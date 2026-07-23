#pragma once

#include <errno.h>
#include <stdint.h>

constexpr uint32_t maxPendingFrameCallbacks = 4096;
constexpr uint32_t maxWaylandClientBuffer = 1024 * 1024;

inline bool transientScanoutError(int error) {
    return error == EBUSY || error == EAGAIN || error == EINTR || error == ENOMEM;
}
