#include "renderer.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "dmabuf.hpp"
#include "server.hpp"

namespace {

#define VK_CHECK(x)                                                       \
    do {                                                                  \
        VkResult r_ = (x);                                                \
        if (r_ != VK_SUCCESS) {                                           \
            std::fprintf(stderr, "vk fail %d: %s (%s:%d)\n", r_, #x,      \
                         __FILE__, __LINE__);                             \
            return false;                                                 \
        }                                                                 \
    } while (0)

constexpr VkFormat kFormat = VK_FORMAT_B8G8R8A8_UNORM; // wl_shm ARGB8888 в памяти = BGRA

} // namespace

uint32_t Renderer::find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((type_bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return UINT32_MAX;
}

bool Renderer::create_image(int w, int h, VkImageUsageFlags usage, VkImage& img,
                            VkDeviceMemory& mem) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = kFormat;
    ici.extent = {(uint32_t)w, (uint32_t)h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device_, &ici, nullptr, &img));

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, img, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device_, &mai, nullptr, &mem));
    VK_CHECK(vkBindImageMemory(device_, img, mem, 0));
    return true;
}

bool Renderer::create_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf,
                                  VkDeviceMemory& mem, void** map) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = usage;
    VK_CHECK(vkCreateBuffer(device_, &bci, nullptr, &buf));

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, buf, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = find_memory_type(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(device_, &mai, nullptr, &mem));
    VK_CHECK(vkBindBufferMemory(device_, buf, mem, 0));
    VK_CHECK(vkMapMemory(device_, mem, 0, VK_WHOLE_SIZE, 0, map));
    return true;
}

bool Renderer::init(int width, int height) {
    width_ = width;
    height_ = height;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "imway";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VK_CHECK(vkCreateInstance(&ici, nullptr, &instance_));

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    if (!n) {
        std::fprintf(stderr, "нет vulkan-устройств\n");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance_, &n, devs.data());
    phys_ = devs[0];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys_, &props);
    std::printf("imway: vulkan device: %s\n", props.deviceName);

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, qf.data());
    queue_family_ = UINT32_MAX;
    for (uint32_t i = 0; i < qn; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family_ = i;
            break;
        }
    if (queue_family_ == UINT32_MAX) return false;

    // dmabuf-импорт: включаем расширения, если девайс умеет
    std::vector<const char*> dev_exts;
    {
        uint32_t en = 0;
        vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, nullptr);
        std::vector<VkExtensionProperties> eprops(en);
        vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, eprops.data());
        auto have = [&](const char* name) {
            for (const auto& e : eprops)
                if (!strcmp(e.extensionName, name)) return true;
            return false;
        };
        const char* need[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                              VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                              VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME};
        has_dmabuf_ = true;
        for (const char* n : need)
            if (!have(n)) {
                has_dmabuf_ = false;
                std::fprintf(stderr, "imway: vulkan без %s — dmabuf выключен\n", n);
            }
        if (has_dmabuf_) {
            for (const char* n : need) dev_exts.push_back(n);
            if (have(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME))
                dev_exts.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
        }
    }

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queue_family_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (uint32_t)dev_exts.size();
    dci.ppEnabledExtensionNames = dev_exts.data();
    VK_CHECK(vkCreateDevice(phys_, &dci, nullptr, &device_));
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    if (has_dmabuf_) {
        get_memory_fd_props_ = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(
            device_, "vkGetMemoryFdPropertiesKHR");
        if (!get_memory_fd_props_) has_dmabuf_ = false;
    }
    if (has_dmabuf_) query_dmabuf_formats();

    // render target + readback
    if (!create_image(width_, height_,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                      target_, target_memory_))
        return false;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = target_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = kFormat;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device_, &vci, nullptr, &target_view_));

    if (!create_host_buffer((VkDeviceSize)width_ * height_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            readback_, readback_memory_, &readback_map_))
        return false;

    // render pass: clear → imgui → TRANSFER_SRC (для readback в любой момент)
    VkAttachmentDescription att{};
    att.format = kFormat;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    VK_CHECK(vkCreateRenderPass(device_, &rpci, nullptr, &render_pass_));

    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = render_pass_;
    fci.attachmentCount = 1;
    fci.pAttachments = &target_view_;
    fci.width = width_;
    fci.height = height_;
    fci.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device_, &fci, nullptr, &framebuffer_));

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = queue_family_;
    VK_CHECK(vkCreateCommandPool(device_, &cpci, nullptr, &cmd_pool_));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cmd_pool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device_, &cbai, &cmd_));
    VkFenceCreateInfo fenci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(device_, &fenci, nullptr, &fence_));

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device_, &sci, nullptr, &sampler_));

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // не писать imgui.ini
    io.DisplaySize = ImVec2((float)width_, (float)height_);
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors; // ресайз за края окна

    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_2;
    ii.Instance = instance_;
    ii.PhysicalDevice = phys_;
    ii.Device = device_;
    ii.QueueFamily = queue_family_;
    ii.Queue = queue_;
    ii.DescriptorPoolSize = 512; // внутренний пул: fonts + AddTexture
    ii.MinImageCount = 2;
    ii.ImageCount = 2;
    ii.PipelineInfoMain.RenderPass = render_pass_;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&ii)) {
        std::fprintf(stderr, "ImGui_ImplVulkan_Init fail\n");
        return false;
    }
    return true;
}

void Renderer::upload_surface(Surface& s) {
    if (s.width <= 0 || s.height <= 0) return;
    SurfaceTexture* tex = s.texture;
    if (tex && (tex->w != s.width || tex->h != s.height)) {
        destroy_texture(tex);
        tex = nullptr;
        s.texture = nullptr;
    }
    if (!tex) {
        tex = new SurfaceTexture();
        tex->w = s.width;
        tex->h = s.height;
        if (!create_image(s.width, s.height,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          tex->image, tex->memory) ||
            !create_host_buffer((VkDeviceSize)s.width * s.height * 4,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tex->staging,
                                tex->staging_memory, &tex->staging_map)) {
            delete tex;
            return;
        }
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = tex->image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = kFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device_, &vci, nullptr, &tex->view);
        tex->ds = ImGui_ImplVulkan_AddTexture(sampler_, tex->view,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        textures_.push_back(tex);
        s.texture = tex;
    }
    std::memcpy(tex->staging_map, s.pixels.data(), s.pixels.size());
    tex->needs_upload = true;
}

void Renderer::destroy_texture(SurfaceTexture* tex) {
    if (!tex) return;
    vkQueueWaitIdle(queue_); // M1: грубо, но корректно
    if (tex->ds) ImGui_ImplVulkan_RemoveTexture(tex->ds);
    if (tex->view) vkDestroyImageView(device_, tex->view, nullptr);
    if (tex->image) vkDestroyImage(device_, tex->image, nullptr);
    if (tex->memory) vkFreeMemory(device_, tex->memory, nullptr);
    if (tex->staging) vkDestroyBuffer(device_, tex->staging, nullptr);
    if (tex->staging_memory) vkFreeMemory(device_, tex->staging_memory, nullptr);
    textures_.remove(tex);
    delete tex;
}

void Renderer::build_ui(Server& server) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width_, (float)height_);
    io.DeltaTime = (float)(1.0 / server.hz);

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar()) {
        // дефолтный шрифт ImGui — только ASCII; кириллица придёт со своим шрифтом
        ImGui::TextUnformatted("imway");
        ImGui::Separator();
        ImGui::Text("clients: %zu", server.toplevels.size());
        ImGui::Separator();
        ImGui::Text("frame %d", server.frames_done);
        ImGui::EndMainMenuBar();
    }

    int i = 0;
    for (Toplevel* t : server.toplevels) {
        Surface* root = t->xdg ? t->xdg->surface : nullptr;
        if (!t->mapped || !root || !root->texture) {
            if (root) mark_tree_unhovered(*root);
            continue;
        }
        char label[256];
        std::snprintf(label, sizeof label, "%s###toplevel%llu", t->title.c_str(),
                      (unsigned long long)t->id);
        ImGui::SetNextWindowPos(ImVec2(40.f + 30.f * i, 60.f + 30.f * i), ImGuiCond_FirstUseEver);
        i++;
        if (!t->win_size_set) {
            // окно под размер первого буфера; дальше размером владеет пользователь
            const ImGuiStyle& st = ImGui::GetStyle();
            ImGui::SetNextWindowSize(ImVec2((float)root->view_w() + st.WindowPadding.x * 2,
                                            (float)root->view_h() + st.WindowPadding.y * 2 +
                                                ImGui::GetFrameHeight()),
                                     ImGuiCond_Always);
            t->win_size_set = true;
        }
        // колесо — клиенту, не скроллу окна
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::Begin(label, nullptr, flags)) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            t->desired_w = (int)avail.x;
            t->desired_h = (int)avail.y;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            draw_surface_tree(*root, origin.x, origin.y);
        } else {
            mark_tree_unhovered(*root);
        }
        ImGui::End();
    }

    // попапы — поверх всех окон через foreground draw list (не отдельные
    // ImGui-окна: их z-order управляется фокусом и проигрывает toplevel'у);
    // позиция позиционера — относительно поверхности родителя, чей img_x/img_y
    // записан этим же кадром
    for (Popup* p : server.popups) {
        Surface* ps = p->xdg ? p->xdg->surface : nullptr;
        if (!p->mapped || !ps || !ps->texture || !p->parent) {
            if (ps) mark_tree_unhovered(*ps);
            continue;
        }
        draw_surface_tree_overlay(*ps, p->parent->img_x + (float)p->x,
                                  p->parent->img_y + (float)p->y);
    }

    ImGui::Render();
}

// вариант draw_surface_tree для попапов: рисует в foreground draw list,
// hovered считается вручную (попапы всегда верхние, перекрыть их некому)
void Renderer::draw_surface_tree_overlay(Surface& s, float x, float y) {
    for (Subsurface* c : s.stack_below)
        if (c->surface && c->surface->has_content)
            draw_surface_tree_overlay(*c->surface, x + (float)c->x, y + (float)c->y);

    if (s.texture) {
        ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
        if (s.vp.has_src && s.texture->w > 0 && s.texture->h > 0) {
            uv0 = ImVec2((float)(s.vp.sx / s.texture->w), (float)(s.vp.sy / s.texture->h));
            uv1 = ImVec2((float)((s.vp.sx + s.vp.sw) / s.texture->w),
                         (float)((s.vp.sy + s.vp.sh) / s.texture->h));
        }
        float w = (float)s.view_w(), h = (float)s.view_h();
        ImGui::GetForegroundDrawList()->AddImage((ImTextureID)(uintptr_t)s.texture->ds,
                                                 ImVec2(x, y), ImVec2(x + w, y + h), uv0, uv1);
        s.img_x = x;
        s.img_y = y;
        ImVec2 m = ImGui::GetIO().MousePos;
        s.hovered = m.x >= x && m.y >= y && m.x < x + w && m.y < y + h;
    }

    for (Subsurface* c : s.stack_above)
        if (c->surface && c->surface->has_content)
            draw_surface_tree_overlay(*c->surface, x + (float)c->x, y + (float)c->y);
}

// нарисовать поверхность и её субповерхности (stack_below → сама → stack_above);
// позже нарисованный Image перекрывает ранние, что и даёт правильный z-порядок
void Renderer::draw_surface_tree(Surface& s, float x, float y) {
    for (Subsurface* c : s.stack_below)
        if (c->surface && c->surface->has_content)
            draw_surface_tree(*c->surface, x + (float)c->x, y + (float)c->y);

    if (s.texture) {
        // wp_viewport: source → UV-кроп, destination → размер итема
        ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
        if (s.vp.has_src && s.texture->w > 0 && s.texture->h > 0) {
            uv0 = ImVec2((float)(s.vp.sx / s.texture->w), (float)(s.vp.sy / s.texture->h));
            uv1 = ImVec2((float)((s.vp.sx + s.vp.sw) / s.texture->w),
                         (float)((s.vp.sy + s.vp.sh) / s.texture->h));
        }
        float w = (float)s.view_w(), h = (float)s.view_h();
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::Image((ImTextureID)(uintptr_t)s.texture->ds, ImVec2(w, h), uv0, uv1);
        s.img_x = x;
        s.img_y = y;
        // hovered без учёта перекрытия сиблингами — seat берёт последний в порядке отрисовки
        s.hovered = ImGui::IsItemHovered();
    }

    for (Subsurface* c : s.stack_above)
        if (c->surface && c->surface->has_content)
            draw_surface_tree(*c->surface, x + (float)c->x, y + (float)c->y);
}

void Renderer::mark_tree_unhovered(Surface& s) {
    s.hovered = false;
    for (Subsurface* c : s.stack_below)
        if (c->surface) mark_tree_unhovered(*c->surface);
    for (Subsurface* c : s.stack_above)
        if (c->surface) mark_tree_unhovered(*c->surface);
}

void Renderer::render_frame(Server& server) {
    build_ui(server);

    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_, &bi);

    // залить обновлённые текстуры поверхностей
    for (SurfaceTexture* tex : textures_) {
        if (!tex->needs_upload) continue;
        VkImageMemoryBarrier to_dst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        to_dst.srcAccessMask = tex->first_use ? 0 : VK_ACCESS_SHADER_READ_BIT;
        to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_dst.oldLayout =
            tex->first_use ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.image = tex->image;
        to_dst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd_,
                             tex->first_use ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                            : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_dst);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(uint32_t)tex->w, (uint32_t)tex->h, 1};
        vkCmdCopyBufferToImage(cmd_, tex->staging, tex->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier to_read = to_dst;
        to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &to_read);
        tex->needs_upload = false;
        tex->first_use = false;
    }

    VkClearValue clear{};
    clear.color = {{0.08f, 0.08f, 0.10f, 1.f}};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = render_pass_;
    rbi.framebuffer = framebuffer_;
    rbi.renderArea = {{0, 0}, {(uint32_t)width_, (uint32_t)height_}};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd_, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_);
    vkCmdEndRenderPass(cmd_);

    // target уже в TRANSFER_SRC (finalLayout) — копия в readback каждый кадр дёшева на cpu-девайсе
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {(uint32_t)width_, (uint32_t)height_, 1};
    vkCmdCopyImageToBuffer(cmd_, target_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1,
                           &region);

    vkEndCommandBuffer(cmd_);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    vkQueueSubmit(queue_, 1, &si, fence_);
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &fence_);
}

bool Renderer::screenshot(const char* path) {
    // readback_map_ обновлён последним кадром: BGRA → PPM (RGB)
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", width_, height_);
    auto* px = (const uint8_t*)readback_map_;
    std::vector<uint8_t> row((size_t)width_ * 3);
    for (int y = 0; y < height_; y++) {
        const uint8_t* src = px + (size_t)y * width_ * 4;
        for (int x = 0; x < width_; x++) {
            row[x * 3 + 0] = src[x * 4 + 2]; // R
            row[x * 3 + 1] = src[x * 4 + 1]; // G
            row[x * 3 + 2] = src[x * 4 + 0]; // B
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

// оба fourcc — B8G8R8A8_UNORM в памяти; X-вариант получает alpha=1 свизлом вью
static constexpr uint32_t kFourccArgb = 0x34325241; // DRM_FORMAT_ARGB8888 ('AR24')
static constexpr uint32_t kFourccXrgb = 0x34325258; // DRM_FORMAT_XRGB8888 ('XR24')

void Renderer::query_dmabuf_formats() {
    VkDrmFormatModifierPropertiesListEXT mod_list{
        VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
    VkFormatProperties2 props{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
    props.pNext = &mod_list;
    vkGetPhysicalDeviceFormatProperties2(phys_, kFormat, &props);
    std::vector<VkDrmFormatModifierPropertiesEXT> mods(mod_list.drmFormatModifierCount);
    mod_list.pDrmFormatModifierProperties = mods.data();
    vkGetPhysicalDeviceFormatProperties2(phys_, kFormat, &props);

    for (const auto& m : mods) {
        if (m.drmFormatModifierPlaneCount != 1) continue; // импортим только 1 плоскость
        if (!(m.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) continue;
        dmabuf_formats_.push_back({kFourccArgb, m.drmFormatModifier});
        dmabuf_formats_.push_back({kFourccXrgb, m.drmFormatModifier});
    }
    std::printf("imway: dmabuf-форматов: %zu (модификаторов на fourcc: %zu)\n",
                dmabuf_formats_.size(), dmabuf_formats_.size() / 2);
}

bool Renderer::dmabuf_format_supported(uint32_t fourcc, uint64_t modifier) const {
    for (const auto& f : dmabuf_formats_)
        if (f.fourcc == fourcc && f.modifier == modifier) return true;
    return false;
}

bool Renderer::import_dmabuf(Surface& s) {
    DmabufBuffer* b = s.dmabuf_buffer ? dmabuf_from_buffer_resource(s.dmabuf_buffer) : nullptr;
    if (!b || !has_dmabuf_) return false;
    if (b->nplanes != 1) return false; // отфильтровано на create, но перестрахуемся

    destroy_texture(s.texture);
    s.texture = nullptr;

    auto* tex = new SurfaceTexture();
    tex->w = b->width;
    tex->h = b->height;
    tex->external = true;

    VkSubresourceLayout plane{};
    plane.offset = b->offsets[0];
    plane.rowPitch = b->strides[0];

    VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info{
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
    mod_info.drmFormatModifier = b->modifier;
    mod_info.drmFormatModifierPlaneCount = 1;
    mod_info.pPlaneLayouts = &plane;

    VkExternalMemoryImageCreateInfo ext_info{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ext_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    ext_info.pNext = &mod_info;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.pNext = &ext_info;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = kFormat;
    ici.extent = {(uint32_t)b->width, (uint32_t)b->height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ici, nullptr, &tex->image) != VK_SUCCESS) {
        std::fprintf(stderr, "imway: dmabuf vkCreateImage fail\n");
        delete tex;
        return false;
    }

    // память: тип должен подходить и картинке, и самому fd
    int fd = dup(b->fds[0]); // импорт забирает fd себе
    VkMemoryFdPropertiesKHR fd_props{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    if (get_memory_fd_props_(device_, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd,
                             &fd_props) != VK_SUCCESS) {
        std::fprintf(stderr, "imway: vkGetMemoryFdPropertiesKHR fail\n");
        close(fd);
        vkDestroyImage(device_, tex->image, nullptr);
        delete tex;
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, tex->image, &req);
    uint32_t type_bits = req.memoryTypeBits & fd_props.memoryTypeBits;
    uint32_t mem_type = UINT32_MAX;
    for (uint32_t i = 0; i < 32 && mem_type == UINT32_MAX; i++)
        if (type_bits & (1u << i)) mem_type = i;

    VkImportMemoryFdInfoKHR import_info{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import_info.fd = fd;
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = tex->image;
    dedicated.pNext = &import_info;
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.pNext = &dedicated;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mem_type;
    if (mem_type == UINT32_MAX ||
        vkAllocateMemory(device_, &mai, nullptr, &tex->memory) != VK_SUCCESS ||
        vkBindImageMemory(device_, tex->image, tex->memory, 0) != VK_SUCCESS) {
        std::fprintf(stderr, "imway: импорт dmabuf-памяти fail\n");
        close(fd);
        vkDestroyImage(device_, tex->image, nullptr);
        if (tex->memory) vkFreeMemory(device_, tex->memory, nullptr);
        delete tex;
        return false;
    }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = tex->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = kFormat;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (b->format == kFourccXrgb) vci.components.a = VK_COMPONENT_SWIZZLE_ONE;
    vkCreateImageView(device_, &vci, nullptr, &tex->view);

    // одноразовый переход UNDEFINED → SHADER_READ_ONLY
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cmd_pool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer once = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &cbai, &once);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(once, &bi);
    VkImageMemoryBarrier to_read{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_read.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_read.image = tex->image;
    to_read.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(once, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &to_read);
    vkEndCommandBuffer(once);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &once;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, cmd_pool_, 1, &once);

    tex->ds = ImGui_ImplVulkan_AddTexture(sampler_, tex->view,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    tex->first_use = false;
    textures_.push_back(tex);
    s.texture = tex;
    return true;
}

void Renderer::shutdown() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    while (!textures_.empty()) destroy_texture(textures_.front());
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    vkDestroySampler(device_, sampler_, nullptr);
    vkDestroyFence(device_, fence_, nullptr);
    vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    vkDestroyFramebuffer(device_, framebuffer_, nullptr);
    vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (readback_map_) vkUnmapMemory(device_, readback_memory_);
    vkDestroyBuffer(device_, readback_, nullptr);
    vkFreeMemory(device_, readback_memory_, nullptr);
    vkDestroyImageView(device_, target_view_, nullptr);
    vkDestroyImage(device_, target_, nullptr);
    vkFreeMemory(device_, target_memory_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroyInstance(instance_, nullptr);
    device_ = VK_NULL_HANDLE;
}
