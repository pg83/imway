#include "pooled_vk.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    template <typename H, void(VKAPI_PTR* F)(VkDevice, H, const VkAllocationCallbacks*)>
    struct VkBox {
        VkDevice device;
        H handle;

        VkBox(VkDevice d, H h)
            : device(d)
            , handle(h)
        {
        }

        ~VkBox() noexcept {
            if (handle) {
                F(device, handle, nullptr);
            }
        }
    };

    template <typename H, void(VKAPI_PTR* F)(VkDevice, H, const VkAllocationCallbacks*)>
    void put(ObjPool& pool, VkDevice d, H h) {
        pool.make<VkBox<H, F>>(d, h);
    }
}

void pooledVk(ObjPool& pool, VkDevice d, VkImage h) {
    put<VkImage, vkDestroyImage>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkImageView h) {
    put<VkImageView, vkDestroyImageView>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkBuffer h) {
    put<VkBuffer, vkDestroyBuffer>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkDeviceMemory h) {
    put<VkDeviceMemory, vkFreeMemory>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkSampler h) {
    put<VkSampler, vkDestroySampler>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkSemaphore h) {
    put<VkSemaphore, vkDestroySemaphore>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkFence h) {
    put<VkFence, vkDestroyFence>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkPipeline h) {
    put<VkPipeline, vkDestroyPipeline>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkPipelineLayout h) {
    put<VkPipelineLayout, vkDestroyPipelineLayout>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkDescriptorSetLayout h) {
    put<VkDescriptorSetLayout, vkDestroyDescriptorSetLayout>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkDescriptorPool h) {
    put<VkDescriptorPool, vkDestroyDescriptorPool>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkCommandPool h) {
    put<VkCommandPool, vkDestroyCommandPool>(pool, d, h);
}

void pooledVk(ObjPool& pool, VkDevice d, VkShaderModule h) {
    put<VkShaderModule, vkDestroyShaderModule>(pool, d, h);
}
