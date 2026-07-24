#pragma once

#include <std/sys/types.h>
#include <std/lib/vector.h>

struct Icon {
    u64 gen = 0;
    int width = 0;
    int height = 0;
    stl::Vector<u32> argb;
};

struct IconResolver {
    virtual u64 iconTexture(const Icon* icon) = 0;
};
