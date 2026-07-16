#pragma once

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Device;
struct Session;

// kms/drm backend: real display via atomic modesetting, dmabuf scanout
struct DeviceKms {
    static Device* create(stl::ObjPool* pool, struct ev_loop* loop, Session& session, stl::StringView devPath);

    // enumerate the drm devices vulkan can drive
    static void list();
};
