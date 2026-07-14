#pragma once

#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
struct InputSink;
struct DmabufFormat;
struct FrameListener;
struct SessionListener;

struct WaylandConfig {
    const char* socketName = "imway-0";
    const char* xkbLayout = nullptr;
    const char* xkbOptions = nullptr;
    const DmabufFormat* formats = nullptr;
    size_t formatCount = 0;
    unsigned long long mainDevice = 0;
    Output* output = nullptr;
    double dpmsSec = 0;
    int drmFd = -1;
};

struct Wayland {
    virtual void run() = 0;
    virtual InputSink* sink() = 0;
    virtual FrameListener* frameListener() = 0;
    virtual SessionListener* sessionListener() = 0;

    static Wayland* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene, const WaylandConfig&);
};
