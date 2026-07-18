#pragma once

#include <std/str/view.h>

struct Composer;
struct Device;

// kms/drm backend: real display via atomic modesetting, dmabuf scanout
struct DeviceKms {
    static Device* create(Composer& c, stl::StringView devPath);

    // enumerate the drm devices vulkan can drive
    static void list();
};
