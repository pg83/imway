#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct DeviceVk;
struct FrameListener;
struct InputSink;
struct Output;
struct Scene;

struct Renderer {
    virtual InputSink* sink() = 0;

    virtual bool screenshot(const char* path) = 0;

    static Renderer* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene, Output& output, const DeviceVk& vk, FrameListener& listener, int framesLimit);
};
