#pragma once

struct Composer;
struct IconProvider;

// the xdg icon provider: an app_id -> .desktop -> Icon= index plus icon
// names resolved against hicolor scalable and absolute .svg paths, all
// served through the Composer::findIcon registry. Icons are rasterized
// lazily into the pool and cached per store generation; inotify watches on
// the xdg dirs rebuild the index the moment something is installed or
// removed — the next lookup simply resolves against the fresh generation
struct IconStore {
    static IconProvider* create(Composer& c);
};
