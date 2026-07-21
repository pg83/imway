#pragma once

#include <stddef.h>

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
struct Keyboard;
struct DmabufFormat;
struct IconPool;
struct IconStore;

struct Composer;

// device-derived plumbing; the entities come from the Composer
struct WaylandConfig {
    stl::StringView socketName = "imway-0";
    const DmabufFormat* formats = nullptr;
    size_t formatCount = 0;
    // subset the primary plane can scan out (dmabuf feedback scanout tranche)
    const DmabufFormat* scanoutFormats = nullptr;
    size_t scanoutFormatCount = 0;
    unsigned long long mainDevice = 0;
    Output* output = nullptr;
    double dpmsSec = 0;
    int drmFd = -1;
    bool explicitSync = false;
};

struct Wayland {
    virtual void run() = 0;
    static Wayland* create(Composer& c, const WaylandConfig&);
};
