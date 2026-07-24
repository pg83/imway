#pragma once

#include <vulkan/vulkan.h>

struct Composer;
struct DeviceVk;
struct Listener;

// Interactive screenshot handoff.  KMS gives the viewer the composed
// scanout's DMA-BUF; backends without exportable scanout use readback.
struct ScreenshotCapture {
    virtual bool busy() const = 0;
    virtual void request() = 0;
    virtual bool submit(int scanoutIndex, VkImage image, VkImageLayout layout) = 0;
    // the output changed mode: captures after this are at the new size (a
    // capture already in flight finishes at the size it was submitted at)
    virtual void resize(int width, int height) = 0;

    static ScreenshotCapture* create(Composer& c, const DeviceVk& vk, int width, int height, VkFormat format, float uiScale, Listener& ready);
};
