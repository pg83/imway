#pragma once

#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct DmabufFormat;
struct ev_loop;
struct FrameListener;
struct InputSink;
struct Scene;

struct WaylandConfig {
    const char* socketName = "imway-0";
    const DmabufFormat* formats = nullptr;
    size_t formatCount = 0;
};

struct Wayland {
    virtual ~Wayland() noexcept;

    virtual void run() = 0;

    virtual InputSink* sink() = 0;
    virtual FrameListener* frameListener() = 0;

    static Wayland* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene,
                           const WaylandConfig&);
};
