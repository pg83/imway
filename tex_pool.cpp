#include "tex_pool.h"

#include "pooled_vk.h"
#include "util.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    // 1024, 2048, 4096, ... — the first chunk covers ordinary scenes; the
    // doubling keeps the pool count tiny even for a hostile surface flood
    constexpr u32 kFirstChunk = 1024;
    constexpr u32 kMaxShift = 16; // clamp so the chunk size never overflows u32

    struct VkTexturePoolImpl: public VkTexturePool {
        VkTexturePoolImpl(ObjPool& pool, VkDevice device, VkSampler sampler);

        VkDescriptorSet alloc(VkImageView view, VkImageLayout imageLayout, VkDescriptorPool& outPool) override;
        void free(VkDescriptorSet set, VkDescriptorPool pool) override;

        VkDescriptorPool grow();
        void write(VkDescriptorSet set, VkImageView view, VkImageLayout imageLayout);

        ObjPool& pool;
        VkDevice device;
        VkSampler sampler;
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        Vector<VkDescriptorPool> chunks;
    };
}

VkTexturePoolImpl::VkTexturePoolImpl(ObjPool& p, VkDevice d, VkSampler s)
    : pool(p)
    , device(d)
    , sampler(s)
{
    // identical to imgui's texture descriptor set layout (imgui_impl_vulkan:
    // one combined image sampler at binding 0, fragment stage, sampler carried
    // in the write rather than the layout), so imgui binds our sets as-is
    VkDescriptorSetLayoutBinding binding{};

    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

    dlci.bindingCount = 1;
    dlci.pBindings = &binding;
    STD_VERIFY(vkCreateDescriptorSetLayout(device, &dlci, nullptr, &layout) == VK_SUCCESS);
    pooledVk(pool, device, layout);

    grow();
}

VkDescriptorPool VkTexturePoolImpl::grow() {
    u32 shift = chunks.length() < kMaxShift ? (u32)chunks.length() : kMaxShift;
    u32 cap = kFirstChunk << shift;

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cap};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};

    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = cap;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &size;

    VkDescriptorPool p = VK_NULL_HANDLE;

    if (vkCreateDescriptorPool(device, &dpci, nullptr, &p) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    pooledVk(pool, device, p);
    chunks.pushBack(p);

    return p;
}

void VkTexturePoolImpl::write(VkDescriptorSet set, VkImageView view, VkImageLayout imageLayout) {
    VkDescriptorImageInfo image{sampler, view, imageLayout};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};

    w.dstSet = set;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

VkDescriptorSet VkTexturePoolImpl::alloc(VkImageView view, VkImageLayout imageLayout, VkDescriptorPool& outPool) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};

    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;

    // walk the chain; a full or fragmented pool is skipped, a fresh one is
    // grown only when none had room
    for (size_t i = 0; i <= chunks.length(); i++) {
        VkDescriptorPool p = i < chunks.length() ? chunks[i] : grow();

        if (!p) {
            break; // device out of memory even for a new pool
        }

        ai.descriptorPool = p;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult r = vkAllocateDescriptorSets(device, &ai, &set);

        if (r == VK_SUCCESS) {
            write(set, view, imageLayout);
            outPool = p;

            return set;
        }

        if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL) {
            break; // genuine failure, not "this pool is full"
        }
    }

    outPool = VK_NULL_HANDLE;

    return VK_NULL_HANDLE;
}

void VkTexturePoolImpl::free(VkDescriptorSet set, VkDescriptorPool pool) {
    if (set && pool) {
        vkFreeDescriptorSets(device, pool, 1, &set);
    }
}

VkTexturePool* VkTexturePool::create(ObjPool& pool, VkDevice device, VkSampler sampler) {
    return pool.make<VkTexturePoolImpl>(pool, device, sampler);
}
