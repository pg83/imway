#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct Device;

// headless backend: offscreen vulkan target, readback present, no display
struct DeviceHeadless {
    static Device* create(stl::ObjPool* pool, struct ev_loop* loop);
};
