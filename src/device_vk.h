#pragma once

#include <vulkan/vulkan.h>

#include <std/sys/types.h>

inline constexpr VkFormat kVkFormat = VK_FORMAT_B8G8R8A8_UNORM;
inline constexpr u32 kFourccArgb = 0x34325241;
inline constexpr u32 kFourccXrgb = 0x34325258;

struct DeviceVk {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    u32 queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    bool hasDmabuf = false;
    u64 renderDev = 0;
    PFN_vkGetMemoryFdPropertiesKHR getMemoryFdProps = nullptr;
};

struct ScanoutBuffer {
    VkImage image = VK_NULL_HANDLE;
};
