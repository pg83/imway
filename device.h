#pragma once

#include <std/str/view.h>

#include <stddef.h>

#include "color.h"
#include "visitor.h"

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
struct Renderer;
struct DmabufFormat;
struct Session;

struct Device {
    virtual unsigned long long renderDevice() const = 0;
    virtual int drmFd() const = 0;
    virtual bool explicitSyncSupported() const = 0;

    // enumerate the dmabuf formats the render device can sample
    virtual void dmabufFormatsImpl(stl::VisitorFace&& vis) = 0;

    template <typename F>
    void dmabufFormats(F f) {
        dmabufFormatsImpl(visitEach<DmabufFormat>(f));
    }

    virtual Output* createOutput(stl::StringView connector, stl::StringView mode,
                                 const OutputConfiguration& config) = 0;
    virtual Renderer* createRenderer(struct Composer& c, stl::StringView fontPath, float uiScale, int framesLimit) = 0;
};
