#pragma once

#include <vulkan/vulkan.h>

namespace stl {
    class ObjPool;
}

// One-shot vulkan handle registrations: the handle is destroyed when the
// pool dies. The device must outlive the pool — it does: DeviceVk lives in
// the root pool and every entity pool dies earlier. Registration order is
// destruction order in reverse (LIFO), so register images before the views
// that sample them.
void pooledVk(stl::ObjPool& pool, VkDevice d, VkImage h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkImageView h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkBuffer h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkDeviceMemory h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkSampler h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkSemaphore h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkFence h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkPipeline h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkPipelineLayout h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkDescriptorSetLayout h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkDescriptorPool h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkCommandPool h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkShaderModule h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkRenderPass h);
void pooledVk(stl::ObjPool& pool, VkDevice d, VkFramebuffer h);
