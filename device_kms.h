#pragma once

#include <std/str/view.h>

struct Composer;
struct Device;

// kms/drm backend: real display via atomic modesetting, dmabuf scanout.
// With Composer::kmsIntercept set, the userspace KMS emulator replaces
// the card node; the backend itself does not care which one it drives.
struct DeviceKms {
    static Device* create(Composer& c, stl::StringView devPath);

    // enumerate the drm devices vulkan can drive
    static void list();
};
