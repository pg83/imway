#pragma once

#include <std/lib/node.h>
#include <std/sys/types.h>

struct FrameListener: stl::IntrusiveNode {
    virtual void frameShown(u32 msec) = 0;
};
