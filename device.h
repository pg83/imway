#pragma once

#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
struct Renderer;
struct DmabufFormat;
struct FrameListener;
struct Session;

struct Device {
    virtual unsigned long long renderDevice() const = 0;
    virtual int drmFd() const = 0;

    virtual size_t dmabufFormatCount() const = 0;
    virtual DmabufFormat dmabufFormat(size_t i) const = 0;

    virtual Output* createOutput(const char* connector, const char* mode, double hdrNits) = 0;
    virtual Renderer* createRenderer(Scene& scene, Output& output, FrameListener& listener, struct IconStore& icons, struct Keyboard& kb, struct InputSink& slave, const char* fontPath, float uiScale, int framesLimit) = 0;

    static Device* createKms(stl::ObjPool* pool, struct ev_loop* loop, Session& session, const char* devPath);
    static Device* createHeadless(stl::ObjPool* pool, struct ev_loop* loop);

    static void list();
};
