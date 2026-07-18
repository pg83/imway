#include "icon.h"
#include "composer.h"
#include "icon_store.h"
#include "intr_list.h"
#include "listener.h"
#include "pooled_ev.h"
#include "pooled_fd.h"
#include "icon_pool.h"
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
#include <std/sys/fs.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr int kIconPx = 48;

    struct DesktopIcon {
        StringBuilder fileId; // .desktop basename == app_id per spec
        StringBuilder icon;   // raw Icon= value
    };

    struct CachedIcon {
        StringBuilder key;
        Icon* icon = nullptr; // misses are cached as nullptr
    };

    void inoCb(struct ev_loop*, ev_io* w, int);
    void reloadCb(struct ev_loop*, ev_timer* w, int);

    struct IconStoreImpl: public IconStore {
        Composer* c = nullptr;
        struct ev_loop* loop = nullptr;
        IconPool* icons = nullptr;

        // pool-backed: stl::Vector wants trivial elements, the strings live
        // in the objects and recycle with them
        ObjList<DesktopIcon> indexAlloc;
        ObjList<CachedIcon> cacheAlloc;
        Vector<DesktopIcon*> index;
        Vector<CachedIcon*> cache;

        int inoFd = -1;
        ev_timer* reloadTimer = nullptr;

        // scratch for case-folding an app_id and each candidate file id in
        // forAppId; a client can set an arbitrarily long app_id, so these must
        // grow rather than overflow a fixed stack buffer
        Buffer appIdLower;
        Buffer fileIdLower;

        IconStoreImpl(Composer& comp);
        ~IconStoreImpl() noexcept;

        void clearCaches();

        void buildIndex();
        void addDesktop(StringBuilder& file, StringView fileId);
        void drainInotify();
        void reload();
        Icon* loadSvgFile(StringBuilder& path);
        Icon* cached(StringView key, auto&& load);
        Icon* byName(StringView name) override;
        Icon* forIconValue(StringView v) override;
        Icon* forAppId(StringView appId) override;
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
    , indexAlloc(comp.pool)
    , cacheAlloc(comp.pool)
{
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

// the caches walk the impl's own members, so their teardown lives in the
// destructor — pool-registered cleanups run after the impl is gone
IconStoreImpl::~IconStoreImpl() noexcept {
    clearCaches();
}

void IconStoreImpl::clearCaches() {
    for (CachedIcon* cached : cache) {
        if (cached->icon) {
            icons->release(cached->icon);
        }

        cacheAlloc.release(cached);
    }

    cache.clear();

    for (DesktopIcon* desktop : index) {
        indexAlloc.release(desktop);
    }

    index.clear();
}

void IconStoreImpl::buildIndex() {
    for (DesktopIcon* di : index) {
        indexAlloc.release(di);
    }

    index.clear();

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
    });
}

void IconStoreImpl::addDesktop(StringBuilder& file, StringView fileId) {
    Buffer data;

    readFileContent(file, data);

    bool inSection = false;
    StringView rest = sv(data);
    DesktopIcon* di = indexAlloc.make();

    di->fileId << fileId;

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
            di->icon << val;

            break;
        }
    }

    if (!di->icon.empty()) {
        index.pushBack(di);
    } else {
        indexAlloc.release(di);
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

    // the old icons go back to the pool only after the subscriber
    // has re-resolved everything onto fresh ones
    Vector<Icon*> old;

    for (CachedIcon* c : cache) {
        if (c->icon) {
            old.pushBack(c->icon);
        }

        cacheAlloc.release(c);
    }

    cache.clear();
    buildIndex();

    forEach<Listener>(c->iconListeners, [](Listener& listener) {
        listener.onListen();
    });

    for (Icon* ic : old) {
        icons->release(ic);
    }

    sysO << "imway: icon store reloaded, "_sv << (u64)index.length() << " entries"_sv << endL;
}

Icon* IconStoreImpl::loadSvgFile(StringBuilder& path) {
    auto doc = lunasvg::Document::loadFromFile(path.cStr());

    if (!doc) {
        return nullptr;
    }

    lunasvg::Bitmap bmp = doc->renderToBitmap(kIconPx, kIconPx);

    if (bmp.isNull()) {
        return nullptr;
    }

    // lunasvg bitmaps are premultiplied ARGB32, same as Icon wants
    Icon* ic = icons->acquire();

    ic->width = kIconPx;
    ic->height = kIconPx;
    ic->argb.append((const u32*)bmp.data(), (size_t)kIconPx * kIconPx);

    return ic;
}

// abbreviated function template: must precede the definitions that call it
Icon* IconStoreImpl::cached(StringView key, auto&& load) {
    for (const CachedIcon* c : cache) {
        if (sv(c->key) == key) {
            return c->icon;
        }
    }

    CachedIcon* c = cacheAlloc.make();

    c->key << key;
    c->icon = load();
    cache.pushBack(c);

    return c->icon;
}

Icon* IconStoreImpl::byName(StringView name) {
    if (name.empty()) {
        return nullptr;
    }

    return cached(name, [this, name]() -> Icon* {
        Icon* found = nullptr;

        forEachXdgDataDir([this, name, &found](StringView base) {
            if (found) {
                return;
            }

            auto& p = sb();

            p << base << "/icons/hicolor/scalable/apps/"_sv << name << ".svg"_sv;

            if (access(p.cStr(), R_OK) == 0) {
                found = loadSvgFile(p);
            }
        });

        return found;
    });
}

Icon* IconStoreImpl::forIconValue(StringView v) {
    if (v.empty()) {
        return nullptr;
    }

    if (v[0] != '/') {
        return byName(v);
    }

    if (!v.endsWith(".svg"_sv)) {
        return nullptr;
    }

    return cached(v, [this, v]() -> Icon* {
        auto& p = sb();

        p << v;

        return loadSvgFile(p);
    });
}

Icon* IconStoreImpl::forAppId(StringView appId) {
    if (appId.empty()) {
        return nullptr;
    }

    StringView al = appId.lower(appIdLower);

    for (const DesktopIcon* di : index) {
        if (sv(di->fileId).lower(fileIdLower) == al) {
            return forIconValue(sv(di->icon));
        }
    }

    return nullptr;
}

IconStore* IconStore::create(Composer& c) {
    return c.pool->make<IconStoreImpl>(c);
}
