#pragma once

#include "input.h"

namespace stl {
    class ObjPool;
}

struct DmabufFormat;
struct ev_loop;
struct Output;
struct Scene;

struct FrameListener {
    virtual void frameShown(u32 msec) = 0;
};

struct Renderer: public InputSink {
    virtual ~Renderer() noexcept;

    virtual size_t dmabufFormatCount() const = 0;
    virtual DmabufFormat dmabufFormat(size_t i) const = 0;

    virtual void setFrameListener(FrameListener*) = 0;

    virtual bool screenshot(const char* path) = 0;

    static Renderer* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene,
                            Output& output, int framesLimit);
};
