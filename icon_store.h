#pragma once

#include <std/str/view.h>

struct Composer;
struct Icon;

// desktop icon manager: the app_id -> .desktop -> Icon= index is built up
// front, icons are rasterized into the pool and cached on first hit, and
// inotify watches on the xdg dirs rebuild the index (and re-resolve every
// window's icon) the moment something is installed or removed; the icons
// returned here are owned by the store, nullptr means "no icon"
struct IconStore {
    virtual Icon* forAppId(stl::StringView appId) = 0;

    // a bare icon name, resolved against hicolor scalable
    virtual Icon* byName(stl::StringView name) = 0;

    // an Icon= value: absolute path or bare name
    virtual Icon* forIconValue(stl::StringView v) = 0;

    static IconStore* create(Composer& c);
};
