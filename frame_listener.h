#pragma once

#include <std/sys/types.h>

// a frame reached the screen: the page flip's timestamp (CLOCK_MONOTONIC)
// and vblank sequence, as the kernel reported them
struct FrameEvent {
    u64 nsec = 0;
    u32 seq = 0;

    u32 msec() const;
};
