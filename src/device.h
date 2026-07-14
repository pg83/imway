#pragma once

#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct DmabufFormat;
struct FrameListener;
struct Output;
struct Renderer;
struct Scene;

struct Device {
    virtual size_t dmabufFormatCount() const = 0;
    virtual DmabufFormat dmabufFormat(size_t i) const = 0;

    virtual Output* createOutput(const char* connector, const char* mode) = 0;
    virtual Renderer* createRenderer(Scene& scene, Output& output, FrameListener& listener, int framesLimit) = 0;

    static Device* createKms(stl::ObjPool* pool, struct ev_loop* loop, const char* devPath);
    static Device* createHeadless(stl::ObjPool* pool, struct ev_loop* loop);

    static void list();
};
