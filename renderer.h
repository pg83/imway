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
struct Composer;

struct Renderer {
    virtual InputSink* sink() = 0;
    virtual bool screenshot(stl::StringView path) = 0;

    static Renderer* create(Composer& c, const DeviceVk& vk, stl::StringView fontPath, float uiScale, int framesLimit);
};
