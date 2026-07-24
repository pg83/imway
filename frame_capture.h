#pragma once

#include <stddef.h>

struct Listener;

// rows handed to a capture consumer: the last presented frame's pixels,
// XRGB8888, base already offset to the requested region
struct CaptureRows {
    const unsigned char* base = nullptr;
    size_t stride = 0;
};

// The renderer's asynchronous readback of the last presented frame,
// consumed by the copy-capture protocols. captureSubmit registers a
// consumer for the current frame's pixels: one GPU copy per frame serves
// every consumer, and done.onListen fires on the loop with a CaptureRows*
// once the copy retires — or with nullptr when it failed. false = no
// pixels right now (a direct-scanout frame forces composition for the
// next one, or an older frame's copy is still in flight): the caller
// keeps the request armed and retries.
struct FrameCapture {
    virtual bool captureSubmit(int x, int y, int w, int h, Listener& done) = 0;
    // the consumer died: detach it; the readback completes and is dropped
    virtual void captureCancel(Listener& done) = 0;
};
