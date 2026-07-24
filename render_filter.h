#pragma once

#include "imgui_wm.h"

#include <std/lib/node.h>

#include <vulkan/vulkan.h>

struct VkTexturePool;

// Borrowed state for one recorded frame. Filters may replace the normal
// output path and mark the context handled; finish() preserves the old direct
// ImGui path when none did.
struct RenderContext {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampler sampler = VK_NULL_HANDLE;
    VkRenderPass outputPass = VK_NULL_HANDLE;
    VkFramebuffer outputFramebuffer = VK_NULL_HANDLE;
    VkTexturePool* textures = nullptr;
    ImDrawData* drawData = nullptr;
    float clearColor[4] = {0.08f, 0.08f, 0.10f, 1.f};
    int width = 0, height = 0;
    bool handled = false;

    void finish();
};

struct Filter: stl::IntrusiveNode {
    virtual void apply(RenderContext& ctx) = 0;
};
