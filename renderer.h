#pragma once

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
struct DeviceVk;
struct Keyboard;
struct InputSink;
struct FrameListener;
struct IconStore;

struct Renderer {
    virtual InputSink* sink() = 0;
    virtual bool screenshot(stl::StringView path) = 0;

    static Renderer* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene, Output& output, const DeviceVk& vk, FrameListener& listener, IconStore& icons, Keyboard& kb, InputSink& slave, stl::StringView fontPath, float uiScale, int framesLimit);
};
