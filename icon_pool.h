#pragma once

#include <std/lib/vector.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

// one icon: premultiplied 0xAARRGGBB pixels; gen is a program-wide unique
// id bumped on every acquire, so a recycled slot never aliases its previous
// life — the renderer keys its texture cache off gen, not the pointer
struct Icon {
    u64 gen = 0;
    int width = 0, height = 0;
    stl::Vector<u32> argb;
};

// owns every icon in the program; wayland and the icon store acquire and
// release here, pixel buffers are recycled together with the slot
struct IconPool {
    virtual Icon* acquire() = 0;
    virtual void release(Icon* icon) = 0;

    static IconPool* create(stl::ObjPool* pool);
};
