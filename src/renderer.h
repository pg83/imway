#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
struct DeviceVk;
struct InputSink;
struct FrameListener;

struct Renderer {
    virtual InputSink* sink() = 0;
    virtual bool screenshot(const char* path) = 0;

    static Renderer* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene, Output& output, const DeviceVk& vk, FrameListener& listener, int framesLimit);
};
