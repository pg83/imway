// Vulkan headless рендерер: offscreen target + ImGui + текстуры поверхностей.
#pragma once

#include <cstdint>
#include <list>

#include <vulkan/vulkan.h>

struct Server;
struct Surface;
struct Toplevel;

struct SurfaceTexture {
    int w = 0, h = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void* staging_map = nullptr;
    VkDescriptorSet ds = VK_NULL_HANDLE; // ImTextureID
    bool needs_upload = false;
    bool first_use = true; // layout ещё UNDEFINED
};

struct Renderer {
    bool init(int width, int height);
    void shutdown();

    // скопировать пиксели поверхности в staging (пересоздать текстуру при ресайзе)
    void upload_surface(Toplevel&, Surface&);
    void destroy_texture(SurfaceTexture*);

    // построить ImGui-кадр по toplevel'ам сервера, записать и исполнить command buffer
    void render_frame(Server&);

    bool screenshot(const char* path);

    // пиксели последнего кадра (BGRA, плотные строки) — источник для scanout
    const void* readback_data() const { return readback_map_; }

private:
    int width_ = 0, height_ = 0;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkImage target_ = VK_NULL_HANDLE;
    VkDeviceMemory target_memory_ = VK_NULL_HANDLE;
    VkImageView target_view_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    VkBuffer readback_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
    void* readback_map_ = nullptr;

    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    std::list<SurfaceTexture*> textures_;

    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props);
    bool create_image(int w, int h, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem);
    bool create_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf,
                            VkDeviceMemory& mem, void** map);
    void build_ui(Server&);
};
