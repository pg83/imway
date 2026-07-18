#pragma once

struct Composer;
struct Device;

// headless backend: offscreen vulkan target, readback present, no display
struct DeviceHeadless {
    static Device* create(Composer& c);
};
