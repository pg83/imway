#include "main_screenshot.h"
#include "util.h"

#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <std/ios/sys.h>
#include <std/sys/types.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <png.h>

using namespace stl;

// imway screenshot <path>: a standalone GLFW+Vulkan imgui client. the
// compositor writes a frame into a memfd (self-describing IMW1 header + RGBA
// rows) and execs us on /proc/self/fd/N; we read it, let the user drag a crop
// rectangle over it, and save the selection as a png under the pictures dir.
// vulkan's WSI only bridges an existing native window, so GLFW owns the
// window/context/input and imgui's glfw+vulkan backends drive the rest.

namespace {
    // ---- decoded source image (RGBA8, tightly packed) ----
    struct Image {
        u32 w = 0, h = 0;
        u8* px = nullptr; // w*h*4, malloc'd
    };

    constexpr u32 kMagic = 0x31574d49u; // 'IMW1' little-endian

    bool readAll(int fd, void* buf, size_t n) {
        u8* p = (u8*)buf;

        while (n) {
            ssize_t r = read(fd, p, n);

            if (r <= 0) {
                return false;
            }

            p += r;
            n -= (size_t)r;
        }

        return true;
    }

    bool loadImage(StringView path, Image& img) {
        Buffer p(path);
        int fd = open(p.cStr(), O_RDONLY | O_CLOEXEC);

        if (fd < 0) {
            sysE << "imway screenshot: cannot open "_sv << path << endL;

            return false;
        }

        struct {
            u32 magic;
            u32 w;
            u32 h;
        } hdr = {};

        bool ok = readAll(fd, &hdr, sizeof(hdr)) && hdr.magic == kMagic && hdr.w && hdr.h;

        if (ok) {
            size_t bytes = (size_t)hdr.w * hdr.h * 4;

            img.w = hdr.w;
            img.h = hdr.h;
            img.px = (u8*)malloc(bytes);
            ok = img.px && readAll(fd, img.px, bytes);
        }

        close(fd);

        if (!ok) {
            sysE << "imway screenshot: bad or truncated image"_sv << endL;
            free(img.px);
            img.px = nullptr;
        }

        return ok;
    }

    // ---- png output ----
    void mkdirs(const char* path) {
        // mkdir -p: walk each '/' separator and create the prefix
        char buf[1024];

        size_t n = strlen(path);

        if (n >= sizeof(buf)) {
            return;
        }

        memcpy(buf, path, n + 1);

        for (char* s = buf + 1; *s; s++) {
            if (*s == '/') {
                *s = 0;
                mkdir(buf, 0755);
                *s = '/';
            }
        }

        mkdir(buf, 0755);
    }

    // save the [x0,y0,x1,y1) region of img (image px, already clamped) as png.
    bool savePng(const Image& img, int x0, int y0, int x1, int y1, const char* file) {
        int cw = x1 - x0;
        int ch = y1 - y0;

        if (cw <= 0 || ch <= 0) {
            return false;
        }

        FILE* f = fopen(file, "wb");

        if (!f) {
            sysE << "imway screenshot: cannot write file"_sv << endL;

            return false;
        }

        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        png_infop info = png ? png_create_info_struct(png) : nullptr;

        if (!png || !info || setjmp(png_jmpbuf(png))) {
            if (png) {
                png_destroy_write_struct(&png, info ? &info : nullptr);
            }

            fclose(f);

            return false;
        }

        png_init_io(png, f);
        png_set_IHDR(png, info, (u32)cw, (u32)ch, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, info);

        for (int y = 0; y < ch; y++) {
            png_write_row(png, (png_bytep)(img.px + ((size_t)(y0 + y) * img.w + x0) * 4));
        }

        png_write_end(png, nullptr);
        png_destroy_write_struct(&png, &info);
        fclose(f);

        return true;
    }

    // $XDG_PICTURES_DIR/screenshots/imway-YYYYMMDD-HHMMSS.png (fallback ~/Pictures)
    void destPath(char* out, size_t cap) {
        const char* base = getenv("XDG_PICTURES_DIR");
        char home[512];

        if (!base || !*base) {
            const char* h = getenv("HOME");

            snprintf(home, sizeof(home), "%s/Pictures", h ? h : ".");
            base = home;
        }

        char dir[768];

        snprintf(dir, sizeof(dir), "%s/screenshots", base);
        mkdirs(dir);

        time_t t = time(nullptr);
        struct tm tm;

        localtime_r(&t, &tm);

        char stamp[32];

        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        snprintf(out, cap, "%s/imway-%s.png", dir, stamp);
    }

    // ---- vulkan plumbing (mirrors imgui's glfw+vulkan example) ----
    VkAllocationCallbacks* gAlloc = nullptr;
    VkInstance gInstance = VK_NULL_HANDLE;
    VkPhysicalDevice gPhys = VK_NULL_HANDLE;
    VkDevice gDevice = VK_NULL_HANDLE;
    u32 gQueueFamily = (u32)-1;
    VkQueue gQueue = VK_NULL_HANDLE;
    VkDescriptorPool gDescPool = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window gWin;
    u32 gMinImageCount = 2;
    bool gRebuild = false;

    void vkc(VkResult e) {
        if (e < 0) {
            fprintf(stderr, "[vulkan] VkResult = %d\n", e);
            abort();
        }
    }

    void setupVulkan(const char** exts, u32 nexts) {
        VkInstanceCreateInfo ci = {};

        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.enabledExtensionCount = nexts;
        ci.ppEnabledExtensionNames = exts;
        vkc(vkCreateInstance(&ci, gAlloc, &gInstance));

        gPhys = ImGui_ImplVulkanH_SelectPhysicalDevice(gInstance);
        gQueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(gPhys);

        const char* devExts[] = {"VK_KHR_swapchain"};
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi = {};

        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = gQueueFamily;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;

        VkDeviceCreateInfo dci = {};

        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qi;
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = devExts;
        vkc(vkCreateDevice(gPhys, &dci, gAlloc, &gDevice));
        vkGetDeviceQueue(gDevice, gQueueFamily, 0, &gQueue);

        VkDescriptorPoolSize sz = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE};
        VkDescriptorPoolCreateInfo pi = {};

        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets = sz.descriptorCount;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &sz;
        vkc(vkCreateDescriptorPool(gDevice, &pi, gAlloc, &gDescPool));
    }

    void setupVulkanWindow(VkSurfaceKHR surface, int w, int h) {
        ImGui_ImplVulkanH_Window* wd = &gWin;

        wd->Surface = surface;

        VkBool32 sup = VK_FALSE;

        vkGetPhysicalDeviceSurfaceSupportKHR(gPhys, gQueueFamily, surface, &sup);

        if (!sup) {
            fprintf(stderr, "imway screenshot: no WSI support\n");
            exit(1);
        }

        const VkFormat fmts[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};

        wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(gPhys, surface, fmts, 4, VK_COLORSPACE_SRGB_NONLINEAR_KHR);

        VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR};

        wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(gPhys, surface, modes, 1);
        ImGui_ImplVulkanH_CreateOrResizeWindow(gInstance, gPhys, gDevice, wd, gQueueFamily, gAlloc, w, h, gMinImageCount, 0);
    }

    u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mp;

        vkGetPhysicalDeviceMemoryProperties(gPhys, &mp);

        for (u32 i = 0; i < mp.memoryTypeCount; i++) {
            if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }

        return 0;
    }

    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet ds = VK_NULL_HANDLE;
    };

    // upload the RGBA source image into a device-local sampled texture and
    // register it with imgui. one-shot: staging buffer, copy on a transient
    // command buffer, block once — refresh is never needed for a still frame
    void uploadTexture(const Image& img, Texture& tex) {
        VkDeviceSize bytes = (VkDeviceSize)img.w * img.h * 4;

        VkImageCreateInfo ici = {};

        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent = {img.w, img.h, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkc(vkCreateImage(gDevice, &ici, gAlloc, &tex.image));

        VkMemoryRequirements req;

        vkGetImageMemoryRequirements(gDevice, tex.image, &req);

        VkMemoryAllocateInfo mai = {};

        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkc(vkAllocateMemory(gDevice, &mai, gAlloc, &tex.memory));
        vkBindImageMemory(gDevice, tex.image, tex.memory, 0);

        VkBuffer staging;
        VkDeviceMemory stagingMem;
        VkBufferCreateInfo bci = {};

        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = bytes;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkc(vkCreateBuffer(gDevice, &bci, gAlloc, &staging));
        vkGetBufferMemoryRequirements(gDevice, staging, &req);
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkc(vkAllocateMemory(gDevice, &mai, gAlloc, &stagingMem));
        vkBindBufferMemory(gDevice, staging, stagingMem, 0);

        void* map = nullptr;

        vkMapMemory(gDevice, stagingMem, 0, bytes, 0, &map);
        memcpy(map, img.px, bytes);
        vkUnmapMemory(gDevice, stagingMem);

        VkCommandPool pool;
        VkCommandPoolCreateInfo pci = {};

        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = gQueueFamily;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkCreateCommandPool(gDevice, &pci, gAlloc, &pool);

        VkCommandBuffer cmd;
        VkCommandBufferAllocateInfo cbi = {};

        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbi.commandPool = pool;
        cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbi.commandBufferCount = 1;
        vkAllocateCommandBuffers(gDevice, &cbi, &cmd);

        VkCommandBufferBeginInfo begin = {};

        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        VkImageMemoryBarrier bar = {};

        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = tex.image;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

        VkBufferImageCopy copy = {};

        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {img.w, img.h, 1};
        vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit = {};

        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkQueueSubmit(gQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(gQueue);

        vkDestroyCommandPool(gDevice, pool, gAlloc);
        vkDestroyBuffer(gDevice, staging, gAlloc);
        vkFreeMemory(gDevice, stagingMem, gAlloc);

        VkImageViewCreateInfo vci = {};

        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = tex.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(gDevice, &vci, gAlloc, &tex.view);

        VkSamplerCreateInfo sci = {};

        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter = VK_FILTER_LINEAR;
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.minLod = -1000;
        sci.maxLod = 1000;
        vkCreateSampler(gDevice, &sci, gAlloc, &tex.sampler);

        tex.ds = ImGui_ImplVulkan_AddTexture(tex.sampler, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void frameRender(ImDrawData* draw) {
        ImGui_ImplVulkanH_Window* wd = &gWin;
        VkSemaphore acq = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore done = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        VkResult e = vkAcquireNextImageKHR(gDevice, wd->Swapchain, UINT64_MAX, acq, VK_NULL_HANDLE, &wd->FrameIndex);

        if (e == VK_ERROR_OUT_OF_DATE_KHR || e == VK_SUBOPTIMAL_KHR) {
            gRebuild = true;
        }

        if (e == VK_ERROR_OUT_OF_DATE_KHR) {
            return;
        }

        ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];

        vkWaitForFences(gDevice, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        vkResetFences(gDevice, 1, &fd->Fence);
        vkResetCommandPool(gDevice, fd->CommandPool, 0);

        VkCommandBufferBeginInfo bi = {};

        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(fd->CommandBuffer, &bi);

        VkRenderPassBeginInfo rp = {};

        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = wd->RenderPass;
        rp.framebuffer = fd->Framebuffer;
        rp.renderArea.extent.width = wd->Width;
        rp.renderArea.extent.height = wd->Height;
        rp.clearValueCount = 1;
        rp.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

        ImGui_ImplVulkan_RenderDrawData(draw, fd->CommandBuffer);

        vkCmdEndRenderPass(fd->CommandBuffer);

        VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {};

        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &acq;
        si.pWaitDstStageMask = &wait;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &fd->CommandBuffer;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &done;
        vkEndCommandBuffer(fd->CommandBuffer);
        vkQueueSubmit(gQueue, 1, &si, fd->Fence);
    }

    void framePresent() {
        if (gRebuild) {
            return;
        }

        ImGui_ImplVulkanH_Window* wd = &gWin;
        VkSemaphore done = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        VkPresentInfoKHR pi = {};

        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &done;
        pi.swapchainCount = 1;
        pi.pSwapchains = &wd->Swapchain;
        pi.pImageIndices = &wd->FrameIndex;

        VkResult e = vkQueuePresentKHR(gQueue, &pi);

        if (e == VK_ERROR_OUT_OF_DATE_KHR || e == VK_SUBOPTIMAL_KHR) {
            gRebuild = true;
        }

        wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
    }

    // ---- crop interaction ----
    // selection in image px; starts as the whole image, redrawn by drag
    struct Crop {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool dragging = false;
        float dragOx = 0, dragOy = 0;
    };

    float clampf(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // draw the image + crop UI; returns 1 = save, -1 = cancel, 0 = keep going
    int drawUi(const Image& img, Texture& tex, Crop& crop) {
        ImGuiViewport* vp = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);

        int result = 0;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        if (ImGui::Begin("##shot", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
            const float toolbarH = ImGui::GetFrameHeightWithSpacing() + 8.f;
            ImVec2 avail = vp->Size;
            float areaW = avail.x;
            float areaH = avail.y - toolbarH;

            // fit-scale the image, centered, in the area above the toolbar
            float scale = clampf(areaW / (float)img.w, 0.f, areaH / (float)img.h);
            float dispW = (float)img.w * scale;
            float dispH = (float)img.h * scale;
            ImVec2 origin(vp->Pos.x + (areaW - dispW) * 0.5f, vp->Pos.y + (areaH - dispH) * 0.5f);

            ImDrawList* dl = ImGui::GetWindowDrawList();

            dl->AddImage((ImTextureID)tex.ds, origin, ImVec2(origin.x + dispW, origin.y + dispH));

            // map between screen and image px
            auto toScreen = [&](float px, float py) {
                return ImVec2(origin.x + px * scale, origin.y + py * scale);
            };
            auto toImg = [&](float sx, float sy) {
                return ImVec2(clampf((sx - origin.x) / scale, 0.f, (float)img.w), clampf((sy - origin.y) / scale, 0.f, (float)img.h));
            };

            // drag anywhere over the image area to (re)draw the selection
            ImVec2 mouse = ImGui::GetIO().MousePos;
            bool overImage = mouse.x >= origin.x && mouse.x <= origin.x + dispW && mouse.y >= origin.y && mouse.y <= origin.y + dispH;

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && overImage) {
                ImVec2 p = toImg(mouse.x, mouse.y);

                crop.dragOx = p.x;
                crop.dragOy = p.y;
                crop.dragging = true;
            }

            if (crop.dragging) {
                ImVec2 p = toImg(mouse.x, mouse.y);

                crop.x0 = crop.dragOx < p.x ? crop.dragOx : p.x;
                crop.y0 = crop.dragOy < p.y ? crop.dragOy : p.y;
                crop.x1 = crop.dragOx > p.x ? crop.dragOx : p.x;
                crop.y1 = crop.dragOy > p.y ? crop.dragOy : p.y;

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    crop.dragging = false;
                }
            }

            // dim the area outside the selection, outline the selection
            ImVec2 s0 = toScreen(crop.x0, crop.y0);
            ImVec2 s1 = toScreen(crop.x1, crop.y1);
            ImVec2 lo(origin.x, origin.y);
            ImVec2 hi(origin.x + dispW, origin.y + dispH);
            ImU32 dim = IM_COL32(0, 0, 0, 140);

            dl->AddRectFilled(lo, ImVec2(hi.x, s0.y), dim);            // above
            dl->AddRectFilled(ImVec2(lo.x, s1.y), hi, dim);           // below
            dl->AddRectFilled(ImVec2(lo.x, s0.y), s0, dim);           // left
            dl->AddRectFilled(ImVec2(s1.x, s0.y), ImVec2(hi.x, s1.y), dim); // right
            dl->AddRect(s0, s1, IM_COL32(255, 255, 255, 230), 0, 0, 1.5f);

            // toolbar: selection size + actions
            ImGui::SetCursorScreenPos(ImVec2(vp->Pos.x + 8.f, vp->Pos.y + areaH + 4.f));

            int cw = (int)(crop.x1 - crop.x0 + 0.5f);
            int ch = (int)(crop.y1 - crop.y0 + 0.5f);
            auto& text = sb();

            text << (i64)cw << "x"_sv << (i64)ch << "  (drag to select, Enter to save, Esc to cancel)"_sv;
            ImGui::TextUnformatted(text.cStr());
            ImGui::SameLine();

            if (ImGui::Button("Save") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                result = 1;
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                result = -1;
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();

        return result;
    }
}

int mainScreenshot(StringView path) {
    Image img;

    if (!loadImage(path, img)) {
        return 1;
    }

    glfwSetErrorCallback([](int e, const char* d) {
        fprintf(stderr, "GLFW %d: %s\n", e, d);
    });

    if (!glfwInit()) {
        sysE << "imway screenshot: glfwInit failed"_sv << endL;

        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // window fits the image but never exceeds the monitor work area
    int winW = (int)img.w;
    int winH = (int)img.h;

    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
        if (const GLFWvidmode* vm = glfwGetVideoMode(mon)) {
            int maxW = vm->width * 9 / 10;
            int maxH = vm->height * 9 / 10;

            if (winW > maxW) {
                winW = maxW;
            }

            if (winH > maxH) {
                winH = maxH;
            }
        }
    }

    GLFWwindow* window = glfwCreateWindow(winW, winH, "imway screenshot", nullptr, nullptr);

    if (!window || !glfwVulkanSupported()) {
        sysE << "imway screenshot: no vulkan/window"_sv << endL;

        return 1;
    }

    u32 nexts = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&nexts);

    setupVulkan(exts, nexts);

    VkSurfaceKHR surface;

    vkc(glfwCreateWindowSurface(gInstance, window, gAlloc, &surface));

    int fbw, fbh;

    glfwGetFramebufferSize(window, &fbw, &fbh);
    setupVulkanWindow(surface, fbw, fbh);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo ii = {};

    ii.Instance = gInstance;
    ii.PhysicalDevice = gPhys;
    ii.Device = gDevice;
    ii.QueueFamily = gQueueFamily;
    ii.Queue = gQueue;
    ii.DescriptorPool = gDescPool;
    ii.MinImageCount = gMinImageCount;
    ii.ImageCount = gWin.ImageCount;
    ii.PipelineInfoMain.RenderPass = gWin.RenderPass;
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&ii);

    Texture tex;

    uploadTexture(img, tex);

    Crop crop;

    crop.x1 = (float)img.w;
    crop.y1 = (float)img.h;

    int action = 0;

    while (!glfwWindowShouldClose(window) && action == 0) {
        glfwPollEvents();

        int nw, nh;

        glfwGetFramebufferSize(window, &nw, &nh);

        if (nw > 0 && nh > 0 && (gRebuild || gWin.Width != nw || gWin.Height != nh)) {
            ImGui_ImplVulkan_SetMinImageCount(gMinImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(gInstance, gPhys, gDevice, &gWin, gQueueFamily, gAlloc, nw, nh, gMinImageCount, 0);
            gWin.FrameIndex = 0;
            gRebuild = false;
        }

        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            ImGui_ImplGlfw_Sleep(10);

            continue;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        action = drawUi(img, tex, crop);

        ImGui::Render();

        ImDrawData* dd = ImGui::GetDrawData();
        bool minimized = dd->DisplaySize.x <= 0 || dd->DisplaySize.y <= 0;

        gWin.ClearValue.color.float32[0] = 0.1f;
        gWin.ClearValue.color.float32[1] = 0.1f;
        gWin.ClearValue.color.float32[2] = 0.1f;
        gWin.ClearValue.color.float32[3] = 1.0f;

        if (!minimized) {
            frameRender(dd);
            framePresent();
        }
    }

    int rc = 0;

    if (action == 1) {
        int x0 = (int)(clampf(crop.x0, 0, (float)img.w) + 0.5f);
        int y0 = (int)(clampf(crop.y0, 0, (float)img.h) + 0.5f);
        int x1 = (int)(clampf(crop.x1, 0, (float)img.w) + 0.5f);
        int y1 = (int)(clampf(crop.y1, 0, (float)img.h) + 0.5f);

        // an empty selection means the user never dragged — save the whole frame
        if (x1 - x0 < 1 || y1 - y0 < 1) {
            x0 = 0;
            y0 = 0;
            x1 = (int)img.w;
            y1 = (int)img.h;
        }

        char out[1024];

        destPath(out, sizeof(out));

        if (savePng(img, x0, y0, x1, y1, out)) {
            sysO << "imway screenshot: saved "_sv << StringView(out) << endL;
        } else {
            rc = 1;
        }
    }

    vkDeviceWaitIdle(gDevice);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImGui_ImplVulkanH_DestroyWindow(gInstance, gDevice, &gWin, gAlloc);
    vkDestroyDescriptorPool(gDevice, gDescPool, gAlloc);
    vkDestroyDevice(gDevice, gAlloc);
    vkDestroyInstance(gInstance, gAlloc);
    glfwDestroyWindow(window);
    glfwTerminate();
    free(img.px);

    return rc;
}
