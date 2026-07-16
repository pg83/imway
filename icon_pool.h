#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Icon;

// owns every icon in the program; wayland and the icon store acquire and
// release here, pixel buffers are recycled together with the slot
struct IconPool {
    virtual Icon* acquire() = 0;
    virtual void release(Icon* icon) = 0;

    static IconPool* create(stl::ObjPool* pool);
};
