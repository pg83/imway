#pragma once

#include <std/lib/vector.h>
#include <std/sys/types.h>

struct Icon {
    u64 gen = 0;
    int width = 0;
    int height = 0;
    stl::Vector<u32> argb;
};
