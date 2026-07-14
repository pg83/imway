#pragma once

#include <std/sys/types.h>

struct FrameListener {
    virtual void frameShown(u32 msec) = 0;
};
