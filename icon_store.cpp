#include "icon.h"
#include "composer.h"
#include "icon_provider.h"
#include "icon_store.h"
#include "pooled_ev.h"
#include "pooled_fd.h"
#include "icon_pool.h"
#include "scene.h"
#include "util.h"
#include "xdg_utils.h"

#include <fcntl.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <ev.h>
#include <lunasvg.h>

#include <std/ios/fs_utils.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/sym/i_map.h>
#include <std/sys/fs.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr int kIconPx = 48;

    void inoCb(struct ev_loop*, ev_io* w, int);
    void reloadCb(struct ev_loop*, ev_timer* w, int);

    struct IconStoreImpl: public IconProvider {
        Composer* c = nullptr;
        struct ev_loop* loop = nullptr;
        IconPool* icons = nullptr;

        // the store generation: both indexes, the lookup cache and the icon
        // leases of everything resolved since the last reload; a reload
        // builds the next generation and drops this one whole
        ObjPool* gen = nullptr;

        // eager indexes, so a cold precomputed-symbol lookup can still
        // materialize: hash(lower(fileId)) -> the .desktop Icon= value, and
        // hash(svg basename) -> its full path. Rasterization stays lazy.
        IntMap<StringBuilder*>* desktop = nullptr;
        IntMap<StringBuilder*>* names = nullptr;

        // query symbol -> resolved icon, misses cached as nullptr
        IntMap<Icon*>* cache = nullptr;

        int inoFd = -1;
        ev_timer* reloadTimer = nullptr;

        // scratch for case-folding; a client can set an arbitrarily long
        // app_id, so this must grow rather than overflow a stack buffer
        Buffer lowerScratch;

        IconStoreImpl(Composer& comp);
        ~IconStoreImpl() noexcept;

        void buildIndex();
        void addDesktop(StringBuilder& file, StringView fileId);
        void drainInotify();
        void reload();
        Icon* loadSvgFile(StringView path);
        Icon* valueIcon(StringView v);
        Icon* resolveSym(u64 sym);
        Icon* findIcon(u64 sym, StringView id) override;
    };

    void inoCb(struct ev_loop*, ev_io* w, int) {
        ((IconStoreImpl*)w->data)->drainInotify();
    }

    void reloadCb(struct ev_loop*, ev_timer* w, int) {
        ((IconStoreImpl*)w->data)->reload();
    }
}

IconStoreImpl::IconStoreImpl(Composer& comp)
    : c(&comp)
    , loop(comp.loop)
    , icons(comp.iconPool)
{
    gen = ObjPool::fromMemoryRaw();
    desktop = gen->make<IntMap<StringBuilder*>>(gen);
    names = gen->make<IntMap<StringBuilder*>>(gen);
    cache = gen->make<IntMap<Icon*>>(gen);
    buildIndex();

    inoFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

    if (inoFd < 0) {
        return;
    }

    u32 mask = IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_CLOSE_WRITE | IN_ATTRIB;

    forEachXdgDataDir([this, mask](StringView base) {
        StringBuilder p;

        p << base << "/applications"_sv;
        inotify_add_watch(inoFd, p.cStr(), mask);
        p.reset();
        p << base << "/icons/hicolor/scalable/apps"_sv;
        inotify_add_watch(inoFd, p.cStr(), mask);
    });

    pooledFD(*c->pool, inoFd);
    ev_io* ino = createEvIo(*c->pool, loop);

    ev_io_init(ino, inoCb, inoFd, EV_READ);
    ino->data = this;
    ev_io_start(loop, ino);
    reloadTimer = createEvTimer(*c->pool, loop);
    ev_timer_init(reloadTimer, reloadCb, 0.5, 0.5);
    reloadTimer->data = this;
}

// the generation arena releases the icon leases into a pool that outlives
// this impl (IconPool is created before the store)
IconStoreImpl::~IconStoreImpl() noexcept {
    delete gen;
}

void IconStoreImpl::buildIndex() {
    forEachXdgDataDir([this](StringView base) {
        StringBuilder dir;

        dir << base << "/applications"_sv;

        // xdg data dirs routinely do not exist: listDir throws, opendir
        // used to shrug — keep shrugging
        try {
            listDir(sv(dir), [this, &dir](const TPathInfo& e) {
                if (e.isDir || !e.item.endsWith(".desktop"_sv)) {
                    return;
                }

                StringBuilder f;

                f << sv(dir) << "/"_sv << e.item;
                addDesktop(f, e.item.prefix(e.item.length() - 8));
            });
        } catch (...) {
        }

        dir.reset();
        dir << base << "/icons/hicolor/scalable/apps"_sv;

        try {
            listDir(sv(dir), [this, &dir](const TPathInfo& e) {
                if (e.isDir || !e.item.endsWith(".svg"_sv)) {
                    return;
                }

                u64 sym = e.item.prefix(e.item.length() - 4).hash64();

                // first hit wins: XDG_DATA_HOME precedes XDG_DATA_DIRS
                if (!names->find(sym)) {
                    StringBuilder* p = gen->make<StringBuilder>();

                    *p << sv(dir) << "/"_sv << e.item;
                    names->insert(sym, p);
                }
            });
        } catch (...) {
        }
    });
}

void IconStoreImpl::addDesktop(StringBuilder& file, StringView fileId) {
    Buffer data;

    readFileContent(file, data);

    bool inSection = false;
    StringView rest = sv(data);
    StringView value;

    while (!rest.empty()) {
        StringView line, tail;

        if (!rest.split('\n', line, tail)) {
            line = rest;
            tail = {};
        }

        rest = tail;
        line = line.stripCr();

        if (!line.empty() && line[0] == '[') {
            inSection = line == "[Desktop Entry]"_sv;

            continue;
        }

        if (!inSection) {
            continue;
        }

        StringView key, val;

        if (line.split('=', key, val) && key == "Icon"_sv) {
            value = val;

            break;
        }
    }

    if (value.empty()) {
        return;
    }

    u64 sym = fileId.lower(lowerScratch).hash64();

    // first hit wins, matching the name index
    if (!desktop->find(sym)) {
        StringBuilder* v = gen->make<StringBuilder>();

        *v << value;
        desktop->insert(sym, v);
    }
}

void IconStoreImpl::drainInotify() {
    alignas(8) char buf[4096];

    while (read(inoFd, buf, sizeof(buf)) > 0) {
    }

    // installs come in bursts: reload once things settle
    ev_timer_again(loop, reloadTimer);
}

void IconStoreImpl::reload() {
    ev_timer_stop(loop, reloadTimer);

    // nobody holds an Icon* across the loop iteration, so the old generation
    // simply dies: its icon leases return to the pool, and the next lookup
    // resolves against the fresh indexes into the fresh cache
    ObjPool* old = gen;

    gen = ObjPool::fromMemoryRaw();
    desktop = gen->make<IntMap<StringBuilder*>>(gen);
    names = gen->make<IntMap<StringBuilder*>>(gen);
    cache = gen->make<IntMap<Icon*>>(gen);
    buildIndex();
    delete old;

    c->scene->needsFrame = true;
    sysO << "imway: icon store reloaded, "_sv << (u64)desktop->size() << " entries"_sv << endL;
}

Icon* IconStoreImpl::loadSvgFile(StringView path) {
    auto doc = lunasvg::Document::loadFromFile(Buffer(path).cStr());

    if (!doc) {
        return nullptr;
    }

    lunasvg::Bitmap bmp = doc->renderToBitmap(kIconPx, kIconPx);

    if (bmp.isNull()) {
        return nullptr;
    }

    // lunasvg bitmaps are premultiplied ARGB32, same as Icon wants
    Icon* ic = icons->acquire(*gen);

    ic->width = kIconPx;
    ic->height = kIconPx;
    ic->argb.append((const u32*)bmp.data(), (size_t)kIconPx * kIconPx);

    return ic;
}

// a name-or-path Icon= value, cached under its own symbol so an app_id
// lookup and a direct name lookup landing on the same value share one icon
Icon* IconStoreImpl::valueIcon(StringView v) {
    u64 sym = v.hash64();

    if (Icon** hit = cache->find(sym)) {
        return *hit;
    }

    Icon* icon = nullptr;

    if (!v.empty() && v[0] == '/') {
        if (v.endsWith(".svg"_sv)) {
            icon = loadSvgFile(v);
        }
    } else if (StringBuilder** path = names->find(sym)) {
        icon = loadSvgFile(sv(**path));
    }

    cache->insert(sym, icon);

    return icon;
}

// the indexed namespaces: a case-folded app_id symbol or an icon name
// symbol. The .desktop mapping wins when a string is both.
Icon* IconStoreImpl::resolveSym(u64 sym) {
    if (StringBuilder** value = desktop->find(sym)) {
        return valueIcon(sv(**value));
    }

    if (StringBuilder** path = names->find(sym)) {
        return loadSvgFile(sv(**path));
    }

    return nullptr;
}

Icon* IconStoreImpl::findIcon(u64 sym, StringView id) {
    if (Icon** hit = cache->find(sym)) {
        return *hit;
    }

    Icon* icon = resolveSym(sym);

    if (!icon && !id.empty()) {
        // string-form extras the indexes cannot serve: a not-yet-folded
        // app_id and an absolute path
        u64 lsym = id.lower(lowerScratch).hash64();

        if (lsym != sym) {
            icon = resolveSym(lsym);
        }

        if (!icon && id[0] == '/' && id.endsWith(".svg"_sv)) {
            icon = loadSvgFile(id);
        }
    }

    cache->insert(sym, icon);

    return icon;
}

IconProvider* IconStore::create(Composer& c) {
    return c.pool->make<IconStoreImpl>(c);
}
