#include "icon_store.h"

#include "log.h"
#include "icon.h"
#include "util.h"
#include "scene.h"
#include "composer.h"
#include "listener.h"
#include "icon_pool.h"
#include "pooled_ev.h"
#include "pooled_fd.h"
#include "xdg_utils.h"
#include "icon_provider.h"

#include <std/sys/fs.h>
#include <std/ios/sys.h>
#include <std/sym/i_map.h>
#include <std/lib/vector.h>
#include <std/ios/fs_utils.h>
#include <std/mem/obj_pool.h>
#include <std/rng/split_mix_64.h>

#include <ev.h>
#include <png.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <lunasvg.h>
#include <sys/inotify.h>

using namespace stl;

namespace {
    struct IconStoreImpl;

    struct CallIconTheme: Listener {
        IconStoreImpl* parent = nullptr;

        explicit CallIconTheme(IconStoreImpl* p)
            : parent(p)
        {
        }

        void onListen(void*) override;
    };

    // svg rasterization edge: the desired size rounded up to a power of two,
    // so nearby ui sizes share one raster instead of one raster each
    constexpr u32 kIconMinPx = 16;
    constexpr u32 kIconMaxPx = 512;

    u32 iconBucket(u32 desired) {
        u32 b = kIconMinPx;

        while (b < desired && b < kIconMaxPx) {
            b *= 2;
        }

        return b;
    }

    // per-bucket cache key: mix the bucket into the query symbol
    u64 bucketSym(u64 sym, u32 bucket) {
        return sym ^ splitMix64(bucket);
    }

    // "48x48" -> 48; 0 for anything that is not a plain square size dir
    // (scalable, and @2-scaled dirs like "256x256@2", are handled elsewhere
    // or skipped)
    u32 parseSizeDir(StringView name) {
        StringView a, b;

        if (!name.split('x', a, b) || a.empty() || a != b) {
            return 0;
        }

        for (size_t i = 0; i < a.length(); i++) {
            if (a[i] < '0' || a[i] > '9') {
                return 0;
            }
        }

        return (u32)a.stou();
    }

    // one icon name across the hicolor theme: the fixed-size png rasters plus
    // the scalable svg. Rasterization stays lazy; this only holds paths.
    struct IconSource {
        Buffer* path = nullptr;
        u32 size = 0;
    };

    struct IconName {
        stl::Vector<IconSource> pngs;
        Buffer* svg = nullptr;
    };

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
        // hash(icon basename) -> its png sizes and svg. Rasterization stays lazy.
        IntMap<Buffer*>* desktop = nullptr;
        IntMap<IconName*>* names = nullptr;

        // query symbol -> resolved icon, misses cached as nullptr
        IntMap<Icon*>* cache = nullptr;

        int inoFd = -1;
        ev_timer* reloadTimer = nullptr;

        // scratch for case-folding; a client can set an arbitrarily long
        // app_id, so this must grow rather than overflow a stack buffer
        Buffer lowerScratch;

        IconStoreImpl(Composer& comp);
        ~IconStoreImpl() noexcept;

        void addWatches();
        void buildIndex();
        void indexTheme(StringView base, StringView theme);
        void addDesktop(Buffer& file, StringView fileId);
        IconName& nameEntry(u64 sym);
        void drainInotify();
        void reload();
        Icon* loadSvgFile(StringView path, u32 bucket);
        Icon* loadPngFile(StringView path);
        Icon* resolveName(IconName& n, u32 bucket);
        Icon* valueIcon(StringView v, u32 bucket);
        Icon* resolveSym(u64 sym, u32 bucket);
        Icon* findIcon(u64 sym, u32 desired, StringView id) override;
    };

    void CallIconTheme::onListen(void*) {
        parent->reload();
    }

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
    desktop = gen->make<IntMap<Buffer*>>(gen);
    names = gen->make<IntMap<IconName*>>(gen);
    cache = gen->make<IntMap<Icon*>>(gen);
    buildIndex();
    comp.settings->addIconThemeListener(comp.pool->make<CallIconTheme>(this));

    inoFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

    if (inoFd < 0) {
        return;
    }

    addWatches();

    pooledFD(*c->pool, inoFd);
    ev_io* ino = createEvIo(*c->pool, loop);

    ev_io_init(ino, inoCb, inoFd, EV_READ);
    ino->data = this;
    ev_io_start(loop, ino);
    reloadTimer = createEvTimer(*c->pool, loop);
    ev_timer_init(reloadTimer, reloadCb, 0.5, 0.5);
    reloadTimer->data = this;
}

// idempotent (inotify_add_watch returns the existing wd for a known path):
// the constructor arms the watches, and every reload re-arms them so a
// size dir that appeared since last time gets covered. Watches only
// accumulate; a removed dir leaves an inert wd. The png sizes live in many
// hicolor/<NxN>/apps leaves, and inotify is not recursive, so the hicolor
// parent is watched too — a brand-new size dir fires there and the debounced
// reload re-enumerates and arms its leaf. The one gap (a png dropped into a
// just-created size dir before its leaf is watched) resolves on the next
// change or restart, the same best-effort the store already relies on.
void IconStoreImpl::addWatches() {
    u32 mask = IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_CLOSE_WRITE | IN_ATTRIB;

    forEachXdgDataDir([this, mask](StringView base) {
        Buffer p;

        {
            StringBuilder builder((Buffer&&)p);

            builder << base << "/applications"_sv;
            builder.xchg(p);
        }
        inotify_add_watch(inoFd, p.cStr(), mask);
        p.reset();
        StringView themes[] = {c->settings->iconTheme(), "hicolor"_sv};

        for (size_t i = 0; i < 2; i++) {
            if (themes[i].empty() || (i && themes[i] == themes[0])) {
                continue;
            }

            p.reset();
            {
                StringBuilder builder((Buffer&&)p);

                builder << base << "/icons/"_sv << themes[i];
                builder.xchg(p);
            }
            inotify_add_watch(inoFd, p.cStr(), mask);

            try {
                listDir(sv(p), [this, mask, &p](const TPathInfo& e) {
                    if (!e.isDir || (e.item != "scalable"_sv && !parseSizeDir(e.item))) {
                        return;
                    }

                    Buffer apps;
                    StringBuilder builder((Buffer&&)apps);

                    builder << sv(p) << "/"_sv << e.item << "/apps"_sv;
                    builder.xchg(apps);
                    inotify_add_watch(inoFd, apps.cStr(), mask);
                });
            } catch (...) {
            }
        }
    });
}

// the generation arena releases the icon leases into a pool that outlives
// this impl (IconPool is created before the store)
IconStoreImpl::~IconStoreImpl() noexcept {
    delete gen;
}

void IconStoreImpl::buildIndex() {
    forEachXdgDataDir([this](StringView base) {
        Buffer dir;
        StringBuilder builder((Buffer&&)dir);

        builder << base << "/applications"_sv;
        builder.xchg(dir);

        // xdg data dirs routinely do not exist: listDir throws, opendir
        // used to shrug — keep shrugging
        try {
            listDir(sv(dir), [this, &dir](const TPathInfo& e) {
                if (e.isDir || !e.item.endsWith(".desktop"_sv)) {
                    return;
                }

                Buffer f;
                StringBuilder builder((Buffer&&)f);

                builder << sv(dir) << "/"_sv << e.item;
                builder.xchg(f);
                addDesktop(f, e.item.prefix(e.item.length() - 8));
            });
        } catch (...) {
        }

        StringView themes[] = {c->settings->iconTheme(), "hicolor"_sv};

        for (size_t i = 0; i < 2; i++) {
            if (!themes[i].empty() && (!i || themes[i] != themes[0])) {
                indexTheme(base, themes[i]);
            }
        }
    });
}

void IconStoreImpl::indexTheme(StringView base, StringView theme) {
    Buffer dir;
    StringBuilder dirBuilder((Buffer&&)dir);

    dirBuilder << base << "/icons/"_sv << theme << "/scalable/apps"_sv;
    dirBuilder.xchg(dir);

    try {
        listDir(sv(dir), [this, &dir](const TPathInfo& e) {
            if (e.isDir || !e.item.endsWith(".svg"_sv)) {
                return;
            }

            IconName& n = nameEntry(e.item.prefix(e.item.length() - 4).hash64());

            if (!n.svg) {
                n.svg = gen->make<Buffer>();
                StringBuilder path((Buffer&&)*n.svg);

                path << sv(dir) << "/"_sv << e.item;
                path.xchg(*n.svg);
            }
        });
    } catch (...) {
    }

    Buffer root;
    StringBuilder rootBuilder((Buffer&&)root);

    rootBuilder << base << "/icons/"_sv << theme;
    rootBuilder.xchg(root);

    try {
        listDir(sv(root), [this, &root](const TPathInfo& sizeDir) {
            u32 size = sizeDir.isDir ? parseSizeDir(sizeDir.item) : 0;

            if (!size) {
                return;
            }

            Buffer apps;
            StringBuilder builder((Buffer&&)apps);

            builder << sv(root) << "/"_sv << sizeDir.item << "/apps"_sv;
            builder.xchg(apps);

            try {
                listDir(sv(apps), [this, &apps, size](const TPathInfo& e) {
                    if (e.isDir || !e.item.endsWith(".png"_sv)) {
                        return;
                    }

                    IconName& n = nameEntry(e.item.prefix(e.item.length() - 4).hash64());

                    for (const IconSource& source : n.pngs) {
                        if (source.size == size) {
                            return;
                        }
                    }

                    Buffer* path = gen->make<Buffer>();
                    StringBuilder builder((Buffer&&)*path);

                    builder << sv(apps) << "/"_sv << e.item;
                    builder.xchg(*path);
                    n.pngs.pushBack({path, size});
                });
            } catch (...) {
            }
        });
    } catch (...) {
    }
}

IconName& IconStoreImpl::nameEntry(u64 sym) {
    if (IconName** hit = names->find(sym)) {
        return **hit;
    }

    IconName* n = gen->make<IconName>();

    names->insert(sym, n);

    return *n;
}

void IconStoreImpl::addDesktop(Buffer& file, StringView fileId) {
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
        Buffer* v = gen->make<Buffer>();

        v->append(value.data(), value.length());
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
    if (reloadTimer) {
        ev_timer_stop(loop, reloadTimer);
    }

    // nobody holds an Icon* across the loop iteration, so the old generation
    // simply dies: its icon leases return to the pool, and the next lookup
    // resolves against the fresh indexes into the fresh cache
    ObjPool* old = gen;

    gen = ObjPool::fromMemoryRaw();
    desktop = gen->make<IntMap<Buffer*>>(gen);
    names = gen->make<IntMap<IconName*>>(gen);
    cache = gen->make<IntMap<Icon*>>(gen);
    buildIndex();
    delete old;

    // a newly installed theme may have added size dirs; arm their leaves
    if (inoFd >= 0) {
        addWatches();
    }

    c->scene->needsFrame = true;
    *(c->log) << "imway: icon store reloaded, "_sv << (u64)desktop->size() << " entries"_sv << endL;
}

Icon* IconStoreImpl::loadSvgFile(StringView path, u32 bucket) {
    auto doc = lunasvg::Document::loadFromFile(Buffer(path).cStr());

    if (!doc) {
        return nullptr;
    }

    lunasvg::Bitmap bmp = doc->renderToBitmap((int)bucket, (int)bucket);

    if (bmp.isNull()) {
        return nullptr;
    }

    // lunasvg bitmaps are premultiplied ARGB32, same as Icon wants
    Icon* ic = icons->acquire(*gen);

    ic->width = (int)bucket;
    ic->height = (int)bucket;
    ic->argb.append((const u32*)bmp.data(), (size_t)bucket * bucket);

    return ic;
}

Icon* IconStoreImpl::loadPngFile(StringView path) {
    png_image image;

    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_file(&image, Buffer(path).cStr())) {
        return nullptr;
    }

    image.format = PNG_FORMAT_BGRA;

    if (image.width == 0 || image.height == 0 || image.width > 1024 || image.height > 1024) {
        png_image_free(&image);

        return nullptr;
    }

    u32 w = image.width, h = image.height;
    Buffer px;

    px.grow((size_t)w * h * 4);

    if (!png_image_finish_read(&image, nullptr, px.mutData(), 0, nullptr)) {
        png_image_free(&image);

        return nullptr;
    }

    // PNG_FORMAT_BGRA is the icon's byte order but straight-alpha; the
    // renderer blends premultiplied (as lunasvg hands us), so premultiply
    u8* b = (u8*)px.mutData();

    for (size_t i = 0; i < (size_t)w * h; i++) {
        u8* p = b + i * 4;
        u32 a = p[3];

        p[0] = (u8)(p[0] * a / 255);
        p[1] = (u8)(p[1] * a / 255);
        p[2] = (u8)(p[2] * a / 255);
    }

    Icon* ic = icons->acquire(*gen);

    ic->width = (int)w;
    ic->height = (int)h;
    ic->argb.append((const u32*)px.data(), (size_t)w * h);

    return ic;
}

// prefer png: the smallest raster that still covers the bucket wins. svg
// only fills the gap above the largest png; failing that, the largest png
// is the best-effort answer. Selection is a pure function of the bucket, so
// the bucket-keyed cache stays coherent.
Icon* IconStoreImpl::resolveName(IconName& n, u32 bucket) {
    const IconSource* cover = nullptr;
    const IconSource* largest = nullptr;

    for (const IconSource& s : n.pngs) {
        if (s.size >= bucket && (!cover || s.size < cover->size)) {
            cover = &s;
        }

        if (!largest || s.size > largest->size) {
            largest = &s;
        }
    }

    if (cover) {
        return loadPngFile(sv(*cover->path));
    }

    if (n.svg) {
        return loadSvgFile(sv(*n.svg), bucket);
    }

    return largest ? loadPngFile(sv(*largest->path)) : nullptr;
}

// a name-or-path Icon= value, cached under its own symbol so an app_id
// lookup and a direct name lookup landing on the same value share one icon
Icon* IconStoreImpl::valueIcon(StringView v, u32 bucket) {
    u64 sym = v.hash64();

    if (Icon** hit = cache->find(bucketSym(sym, bucket))) {
        return *hit;
    }

    Icon* icon = nullptr;

    if (!v.empty() && v[0] == '/') {
        if (v.endsWith(".svg"_sv)) {
            icon = loadSvgFile(v, bucket);
        } else if (v.endsWith(".png"_sv)) {
            icon = loadPngFile(v);
        }
    } else if (IconName** n = names->find(sym)) {
        icon = resolveName(**n, bucket);
    }

    cache->insert(bucketSym(sym, bucket), icon);

    return icon;
}

// the indexed namespaces: a case-folded app_id symbol or an icon name
// symbol. The .desktop mapping wins when a string is both.
Icon* IconStoreImpl::resolveSym(u64 sym, u32 bucket) {
    if (Buffer** value = desktop->find(sym)) {
        return valueIcon(sv(**value), bucket);
    }

    if (IconName** n = names->find(sym)) {
        return resolveName(**n, bucket);
    }

    return nullptr;
}

Icon* IconStoreImpl::findIcon(u64 sym, u32 desired, StringView id) {
    u32 bucket = iconBucket(desired);

    if (Icon** hit = cache->find(bucketSym(sym, bucket))) {
        return *hit;
    }

    Icon* icon = resolveSym(sym, bucket);

    if (!icon && !id.empty()) {
        // string-form extras the indexes cannot serve: a not-yet-folded
        // app_id and an absolute path
        u64 lsym = id.lower(lowerScratch).hash64();

        if (lsym != sym) {
            icon = resolveSym(lsym, bucket);
        }

        if (!icon && id[0] == '/') {
            if (id.endsWith(".svg"_sv)) {
                icon = loadSvgFile(id, bucket);
            } else if (id.endsWith(".png"_sv)) {
                icon = loadPngFile(id);
            }
        }
    }

    cache->insert(bucketSym(sym, bucket), icon);

    return icon;
}

IconProvider* IconStore::create(Composer& c) {
    return c.pool->make<IconStoreImpl>(c);
}
