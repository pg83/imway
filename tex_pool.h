#pragma once

#include <vulkan/vulkan.h>

namespace stl {
    class ObjPool;
}

// A growable allocator of imgui-compatible texture descriptor sets. imgui's
// own AddTexture draws from a single fixed pool a client can exhaust (one
// descriptor set per mapped surface) and then crash the driver; this owns a
// chain of pools instead. Its descriptor set layout is defined identically to
// imgui's texture layout (main/Y at binding 0 and chroma at binding 1), so the
// sets it returns bind to imgui's pipeline as ImTextureID. RGB textures bind
// their sole view at both bindings.
//
// Each new chunk doubles the previous (1024, 2048, 4096, ...). alloc walks the
// chain and only grows when every existing pool is full; free returns a set to
// its pool so the space is reclaimed, hence alloc takes and free is given the
// owning pool (store it next to the set).
struct VkTexturePool {
    // returns a descriptor set sampling (view, layout), or VK_NULL_HANDLE only
    // on genuine device out-of-memory; outPool receives its owning pool
    virtual VkDescriptorSet alloc(VkImageView view, VkImageLayout layout, VkDescriptorPool& outPool, VkImageView chromaView = VK_NULL_HANDLE) = 0;
    virtual void free(VkDescriptorSet set, VkDescriptorPool pool) = 0;

    static VkTexturePool* create(stl::ObjPool& pool, VkDevice device, VkSampler sampler);
};
