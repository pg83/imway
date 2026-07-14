#pragma once

#include "input_sink.h"

namespace stl {
    class ObjPool;
}

struct DmabufFormat;
struct ev_loop;
struct FrameListener;
struct Output;
struct Scene;

struct Renderer: public InputSink {
    virtual size_t dmabufFormatCount() const = 0;
    virtual DmabufFormat dmabufFormat(size_t i) const = 0;

    virtual void setFrameListener(FrameListener*) = 0;

    virtual bool screenshot(const char* path) = 0;

    static Renderer* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene, Output& output, int framesLimit);
};
