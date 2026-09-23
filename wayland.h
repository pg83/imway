#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
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
    // the render device's 2d image ceiling for client buffers
    u32 maxImageDim = 0;
    Output* output = nullptr;
    int drmFd = -1;
    bool explicitSync = false;
};

struct Wayland {
    virtual void run() = 0;

    // Raw input activity is reported before UI routing, so an overlay which
    // consumes the event (notably the lock screen) cannot prevent DPMS wake.
    virtual void inputActivity() = 0;

    // switch the active xkb group and broadcast the modifier change to the
    // focused client, exactly as a layout hotkey would; group is one of the
    // keyboard's layouts
    virtual void setLayout(u32 group) = 0;

    // Process whatever the clients have already sent. The control harness
    // calls this before every command: a scenario acts the moment a client
    // says it committed, and the loop is free to answer the command first,
    // which would make the command see the state from before that commit.
    virtual void drainClients() = 0;

    static Wayland* create(Composer& c, const WaylandConfig&);
};
