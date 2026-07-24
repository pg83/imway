#include "tex_pool.h"

#include "util.h"
#include "pooled_vk.h"

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

        VkDescriptorSet alloc(VkImageView view, VkImageLayout imageLayout, VkDescriptorPool& outPool, VkImageView chromaView) override;
        void free(VkDescriptorSet set, VkDescriptorPool pool) override;

        VkDescriptorPool grow();
        void write(VkDescriptorSet set, VkImageView view, VkImageView chromaView, VkImageLayout imageLayout);

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
    // identical to imgui's texture descriptor set layout, so imgui binds our
    // sets as-is. Binding 1 carries the interleaved UV plane for NV12/P010.
    VkDescriptorSetLayoutBinding bindings[2]{};

    for (u32 i = 0; i < 2; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

    dlci.bindingCount = 2;
    dlci.pBindings = bindings;
    STD_VERIFY(vkCreateDescriptorSetLayout(device, &dlci, nullptr, &layout) == VK_SUCCESS);
    pooledVk(pool, device, layout);

    grow();
}

VkDescriptorPool VkTexturePoolImpl::grow() {
    u32 shift = chunks.length() < kMaxShift ? (u32)chunks.length() : kMaxShift;
    u32 cap = kFirstChunk << shift;

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cap * 2};
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

void VkTexturePoolImpl::write(VkDescriptorSet set, VkImageView view, VkImageView chromaView, VkImageLayout imageLayout) {
    VkDescriptorImageInfo images[2] = {
        {sampler, view, imageLayout},
        {sampler, chromaView ? chromaView : view, imageLayout},
    };
    VkWriteDescriptorSet writes[2]{};

    for (u32 i = 0; i < 2; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
}

VkDescriptorSet VkTexturePoolImpl::alloc(VkImageView view, VkImageLayout imageLayout, VkDescriptorPool& outPool, VkImageView chromaView) {
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
            write(set, view, chromaView, imageLayout);
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
