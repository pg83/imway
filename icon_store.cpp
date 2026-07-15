#include "icon.h"
#include "icon_store.h"
#include "icon_pool.h"
#include "util.h"
#include "xdg_utils.h"

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <ev.h>
#include <lunasvg.h>

#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr int kIconPx = 48;

    struct DesktopIcon {
        char fileId[128] = ""; // .desktop basename == app_id per spec
        char icon[256] = "";   // raw Icon= value
    };

    struct CachedIcon {
        char key[256] = "";
        Icon* icon = nullptr; // misses are cached as nullptr
    };

    void setStr(char* dst, size_t cap, StringView v) {
        size_t n = v.length() < cap - 1 ? v.length() : cap - 1;

        memcpy(dst, v.data(), n);
        dst[n] = 0;
    }

    void inoCb(struct ev_loop*, ev_io* w, int);
    void reloadCb(struct ev_loop*, ev_timer* w, int);

    struct IconStoreImpl: public IconStore {
        struct ev_loop* loop = nullptr;
        IconPool* icons = nullptr;
        IconStoreListener* listener = nullptr;

        Vector<DesktopIcon> index;
        Vector<CachedIcon> cache;

        int inoFd = -1;
        ev_io inoIo{};
        ev_timer reloadTimer{};

        IconStoreImpl(struct ev_loop* l, IconPool& p);
        ~IconStoreImpl() noexcept;

        void buildIndex();
        void addDesktop(const char* file, StringView fileId);
        void drainInotify();
        void setListener(IconStoreListener* l) override;
        void reload();
        Icon* loadSvgFile(const char* path);
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

IconStoreImpl::IconStoreImpl(struct ev_loop* l, IconPool& p)
    : loop(l)
    , icons(&p)
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

    ev_io_init(&inoIo, inoCb, inoFd, EV_READ);
    inoIo.data = this;
    ev_io_start(loop, &inoIo);
    ev_timer_init(&reloadTimer, reloadCb, 0.5, 0.5);
    reloadTimer.data = this;
}

IconStoreImpl::~IconStoreImpl() noexcept {
    if (inoFd >= 0) {
        ev_io_stop(loop, &inoIo);
        ev_timer_stop(loop, &reloadTimer);
        close(inoFd);
    }
}

void IconStoreImpl::buildIndex() {
    index.clear();

    forEachXdgDataDir([this](StringView base) {
        StringBuilder p;

        p << base << "/applications"_sv;

        DIR* d = opendir(p.cStr());

        if (!d) {
            return;
        }

        size_t mark = p.used();

        while (dirent* de = readdir(d)) {
            StringView n(de->d_name);

            if (!n.endsWith(".desktop"_sv)) {
                continue;
            }

            p.seekAbsolute(mark);
            p << "/"_sv << n;
            addDesktop(p.cStr(), n.prefix(n.length() - 8));
        }

        closedir(d);
    });
}

void IconStoreImpl::addDesktop(const char* file, StringView fileId) {
    int fd = ::open(file, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        return;
    }

    Vector<u8> data;
    u8 buf[4096];

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));

        if (n <= 0) {
            break;
        }

        data.append(buf, (size_t)n);
    }

    close(fd);

    bool inSection = false;
    StringView rest((const u8*)data.data(), data.length());
    DesktopIcon di;

    setStr(di.fileId, sizeof(di.fileId), fileId);

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
            setStr(di.icon, sizeof(di.icon), val);

            break;
        }
    }

    if (di.icon[0]) {
        index.pushBack(di);
    }
}

void IconStoreImpl::drainInotify() {
    alignas(8) char buf[4096];

    while (read(inoFd, buf, sizeof(buf)) > 0) {
    }

    // installs come in bursts: reload once things settle
    ev_timer_again(loop, &reloadTimer);
}

void IconStoreImpl::setListener(IconStoreListener* l) {
    listener = l;
}

void IconStoreImpl::reload() {
    ev_timer_stop(loop, &reloadTimer);

    // the old icons go back to the pool only after the subscriber
    // has re-resolved everything onto fresh ones
    Vector<Icon*> old;

    for (size_t i = 0; i < cache.length(); i++) {
        if (cache[i].icon) {
            old.pushBack(cache[i].icon);
        }
    }

    cache.clear();
    buildIndex();

    if (listener) {
        listener->iconsReloaded();
    }

    for (Icon* ic : old) {
        icons->release(ic);
    }

    sysO << "imway: icon store reloaded, "_sv << (u64)index.length() << " entries"_sv << endL;
}

Icon* IconStoreImpl::loadSvgFile(const char* path) {
    auto doc = lunasvg::Document::loadFromFile(path);

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
    for (size_t i = 0; i < cache.length(); i++) {
        if (StringView(cache[i].key) == key) {
            return cache[i].icon;
        }
    }

    CachedIcon c;

    setStr(c.key, sizeof(c.key), key);
    c.icon = load();
    cache.pushBack(c);

    return c.icon;
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
                found = loadSvgFile(p.cStr());
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

        return loadSvgFile(p.cStr());
    });
}

Icon* IconStoreImpl::forAppId(StringView appId) {
    if (appId.empty()) {
        return nullptr;
    }

    u8 ab[128];
    StringView al = appId.lower(ab);

    for (size_t i = 0; i < index.length(); i++) {
        u8 fb[128];

        if (StringView(index[i].fileId).lower(fb) == al) {
            return forIconValue(StringView(index[i].icon));
        }
    }

    return nullptr;
}

IconStore* IconStore::create(ObjPool* pool, struct ev_loop* loop, IconPool& icons) {
    return pool->make<IconStoreImpl>(loop, icons);
}
