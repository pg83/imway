#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Icon;

// owns every icon in the program; pixel buffers are recycled together with
// the slot. Two ownership forms: the arena form registers the release in the
// caller's ObjPool (the icon returns to the free list when the arena dies —
// generation callers like the icon store never release by hand), the manual
// pair remains for slots whose icon is replaced in place (tray pixmaps,
// toplevel icons)
struct IconPool {
    virtual Icon* acquire(stl::ObjPool& owner) = 0;
    virtual Icon* acquire() = 0;
    virtual void release(Icon* icon) = 0;

    static IconPool* create(stl::ObjPool* pool);
};
