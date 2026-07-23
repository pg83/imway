#pragma once

#include <errno.h>
#include <stdint.h>

constexpr uint32_t maxPendingFrameCallbacks = 4096;
constexpr uint32_t maxWaylandClientBuffer = 1024 * 1024;
constexpr int compositorRestartExitCode = 75;

struct RestartBackoff {
    uint32_t delayMsec = 100;

    uint32_t next(uint64_t runtimeMsec) {
        if (runtimeMsec >= 60000) {
            delayMsec = 100;
        }

        uint32_t result = delayMsec;

        delayMsec = delayMsec >= 6667 ? 10000 : (delayMsec * 3 + 1) / 2;

        return result;
    }
};

inline bool transientScanoutError(int error) {
    return error == EBUSY || error == EAGAIN || error == EINTR || error == ENOMEM;
}
