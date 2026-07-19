#pragma once

#include <vulkan/vulkan.h>

struct Composer;
struct DeviceVk;

// Asynchronous interactive screenshot readback. submit() is called directly
// after the composed frame submission, on the same Vulkan queue.
struct ScreenshotCapture {
    virtual bool busy() const = 0;
    virtual bool submit(VkImage image, VkImageLayout layout) = 0;

    static ScreenshotCapture* create(Composer& c, const DeviceVk& vk,
                                     int width, int height, VkFormat format,
                                     float uiScale);
};
