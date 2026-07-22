#pragma once

#include <stddef.h>

// The renderer's readback of the last presented frame, consumed by the
// copy-capture protocol: XRGB8888 rows, tightly packed per `stride`, sized
// exactly to the scene output. false = no pixels this frame; a
// direct-scanout frame forces composition for the next one, so the caller
// keeps the request armed and retries.
struct FrameCapture {
    virtual bool captureFrame(unsigned char* dst, size_t stride,
                              int x, int y, int w, int h) = 0;
};
