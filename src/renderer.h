#pragma once

#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
struct DmabufFormat;
struct FrameListener;
struct InputSink;

struct Renderer {
    virtual InputSink* sink() = 0;

    virtual size_t dmabufFormatCount() const = 0;
    virtual DmabufFormat dmabufFormat(size_t i) const = 0;

    virtual void setFrameListener(FrameListener*) = 0;

    virtual bool screenshot(const char* path) = 0;

    static Renderer* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene, Output& output, int framesLimit);
};
