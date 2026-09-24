#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Keyboard;
struct DmabufFormat;
struct IconPool;

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
    // the render device's 2d image ceiling for client buffers: Vulkan's
    // maxImageDimension2D, at least 4096 on every device
    u32 maxImageDim = 0;
    int drmFd = -1;
    bool explicitSync = false;
};

struct Wayland {
    virtual void run() = 0;

    // Process whatever the clients have already sent. The control harness
    // calls this before every command: a scenario acts the moment a client
    // says it committed, and the loop is free to answer the command first,
    // which would make the command see the state from before that commit.
    virtual void drainClients() = 0;

    static Wayland* create(Composer& c, const WaylandConfig&);
};
