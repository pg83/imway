#pragma once

#include <vulkan/vulkan.h>

#include <std/dbg/verify.h>
#include <std/str/view.h>
#include <std/sys/types.h>

#define VK_CHECK(x) STD_VERIFY((x) == VK_SUCCESS)

inline constexpr VkFormat kVkFormat = VK_FORMAT_B8G8R8A8_UNORM;
inline constexpr u32 kFourccArgb = 0x34325241;
inline constexpr u32 kFourccXrgb = 0x34325258;

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
};

struct ScanoutBuffer {
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = kVkFormat;
};

struct DmabufFormat;

namespace stl {
    template <typename T> class Vector;
}

// shared vulkan/mode helpers, defined in device_vk.cpp and used by both the
// kms and headless backends
struct ModeSpec {
    int w = 0, h = 0;
    double hz = 0;
};

bool parseModeSpec(stl::StringView s, ModeSpec& m);
void initVulkan(DeviceVk& vk, int drmFd);
void destroyVulkan(DeviceVk& vk) noexcept;
void queryDmabufFormats(const DeviceVk& vk, stl::Vector<DmabufFormat>& out);
