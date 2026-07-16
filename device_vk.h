#pragma once

#include <vulkan/vulkan.h>

#include <std/str/view.h>
#include <std/sys/types.h>

#include "visitor.h"

#define VK_CHECK(x) STD_VERIFY((x) == VK_SUCCESS)

inline constexpr VkFormat kVkFormat = VK_FORMAT_B8G8R8A8_UNORM;
inline constexpr u32 kFourccArgb = 0x34325241;
inline constexpr u32 kFourccXrgb = 0x34325258;

struct DmabufFormat;

struct DeviceVk {
    int drmFd = -1;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    u32 queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    bool hasDmabuf = false;
    bool hasSyncFd = false;
    u64 renderDev = 0;
    PFN_vkGetMemoryFdPropertiesKHR getMemoryFdProps = nullptr;

    // drmFd < 0 picks any vulkan device (headless); otherwise the one that
    // drives that drm node. pool-owned, borrowed by const pointer elsewhere
    DeviceVk(int drmFd);
    ~DeviceVk() noexcept;

    DeviceVk(const DeviceVk&) = delete;
    DeviceVk& operator=(const DeviceVk&) = delete;

    void queryDmabufFormatsImpl(stl::VisitorFace&& vis) const;

    template <typename F>
    void queryDmabufFormats(F f) const {
        queryDmabufFormatsImpl(visitEach<DmabufFormat>(f));
    }
};

struct ScanoutBuffer {
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = kVkFormat;
};

// display mode "WxH@Hz"; hz 0 = don't care
struct ModeSpec {
    int w = 0, h = 0;
    double hz = 0;

    bool parse(stl::StringView s);
};
