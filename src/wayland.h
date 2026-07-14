#pragma once

#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct InputSink;
struct DmabufFormat;
struct FrameListener;

struct WaylandConfig {
    const char* socketName = "imway-0";
    const DmabufFormat* formats = nullptr;
    size_t formatCount = 0;
};

struct Wayland {
    virtual void run() = 0;
    virtual InputSink* sink() = 0;
    virtual FrameListener* frameListener() = 0;

    static Wayland* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene, const WaylandConfig&);
};
