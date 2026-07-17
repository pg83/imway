#include "main_screenshot.h"
#include "util.h"

#include <fcntl.h>
#include <time.h>
#include <float.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <std/ios/sys.h>
#include <std/ios/out_fd.h>
#include <std/ios/fs_utils.h>
#include <std/sys/fd.h>
#include <std/sys/throw.h>
#include <std/sys/types.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#include <wayland-client.h>

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
    // a screenshot-specific exception carrying a human message; raised by fail()
    // and shown verbatim on the error panel. derives stl::Exception so
    // Exception::current() surfaces it in a generic catch, like the rest of imway
    struct ShotError: Exception {
        Buffer msg;

        explicit ShotError(Buffer m)
            : msg((Buffer&&)m)
        {
        }

        ExceptionKind kind() const noexcept override {
            return ExceptionKind::Verify;
        }

        StringView description() override {
            return sv(msg);
        }
    };

    [[noreturn]] void fail(StringView m) {
        throw ShotError(Buffer(m));
    }

    // ---- decoded source image ----
    // the whole memfd (IMW1 header + RGBA8 rows) is read into one buffer; px
    // points past the 12-byte header, so no separate pixel allocation
    struct Image {
        Buffer file;
        u32 w = 0, h = 0;
        const u8* px = nullptr;
    };

    constexpr u32 kMagic = 0x31574d49u; // 'IMW1' little-endian

    // throws (ShotError, or the Errno readFileContent raises) on any failure
    void loadImage(StringView path, Image& img) {
        Buffer p(path);

        readFileContent(p, img.file);

        if (img.file.used() < 12) {
            fail("not an imway screenshot (too small)"_sv);
        }

        const u32* h = (const u32*)img.file.data();

        if (h[0] != kMagic || !h[1] || !h[2]) {
            fail("not an imway screenshot (bad header)"_sv);
        }

        img.w = h[1];
        img.h = h[2];

        if (img.file.used() < 12 + (size_t)img.w * img.h * 4) {
            fail("truncated screenshot"_sv);
        }

        img.px = (const u8*)img.file.data() + 12;
    }

    // ---- png output ----
    // mkdir -p: create each '/'-separated prefix of the path in turn
    void mkdirs(StringView path) {
        Buffer b(path);
        char* s = b.cStr();

        for (char* p = s + 1; *p; p++) {
            if (*p == '/') {
                *p = 0;
                mkdir(s, 0755);
                *p = '/';
            }
        }

        mkdir(s, 0755);
    }

    // libpng writes go through this into a growable buffer, so the same encode
    // path feeds both the file save and the clipboard data source
    void pngWrite(png_structp png, png_bytep data, png_size_t len) {
        ((Buffer*)png_get_io_ptr(png))->append(data, (size_t)len);
    }

    // encode the [x0,y0,x1,y1) region of img (image px, already clamped) as an
    // RGBA png into out; throws on failure
    void encodePng(const Image& img, int x0, int y0, int x1, int y1, Buffer& out) {
        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        png_infop info = png ? png_create_info_struct(png) : nullptr;

        if (!png || !info || setjmp(png_jmpbuf(png))) {
            if (png) {
                png_destroy_write_struct(&png, info ? &info : nullptr);
            }

            fail("png encode failed"_sv);
        }

        png_set_write_fn(png, &out, pngWrite, nullptr);
        png_set_IHDR(png, info, (u32)(x1 - x0), (u32)(y1 - y0), 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, info);

        for (int y = y0; y < y1; y++) {
            png_write_row(png, (png_bytep)(img.px + ((size_t)y * img.w + x0) * 4));
        }

        png_write_end(png, nullptr);
        png_destroy_write_struct(&png, &info);
    }

    // stream the encoded png to a file; throws on open/write failure
    void savePng(const Buffer& png, StringView file) {
        ScopedFD fd(open(Buffer(file).cStr(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));

        if (fd.get() < 0) {
            fail(sv(StringBuilder() << "cannot write "_sv << file));
        }

        FDRegular out(fd);

        out.write(png.data(), png.length());
        out.flush();
    }

    // $XDG_PICTURES_DIR/screenshots/imway-YYYYMMDD-HHMMSS.png (fallback ~/Pictures)
    Buffer destPath() {
        StringBuilder dir;
        const char* base = getenv("XDG_PICTURES_DIR");

        if (base && *base) {
            dir << StringView(base);
        } else {
            const char* home = getenv("HOME");

            dir << StringView(home ? home : ".") << "/Pictures"_sv;
        }

        dir << "/screenshots"_sv;
        mkdirs(sv(dir));

        time_t t = time(nullptr);
        struct tm tm;

        localtime_r(&t, &tm);

        char stamp[32];

        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);

        return Buffer(sv(StringBuilder() << sv(dir) << "/imway-"_sv << StringView(stamp) << ".png"_sv));
    }

    // ---- wayland clipboard ----
    // glfw only speaks the text clipboard, so image/png goes on the wayland
    // selection directly: bind wl_seat + wl_data_device_manager off glfw's own
    // wl_display, offer a data source, and hold it alive to serve paste
    // requests until the selection is taken over — the same "linger until
    // overwritten" contract wl-copy honors.
    struct Clip {
        wl_seat* seat = nullptr;
        wl_data_device_manager* ddm = nullptr;
        wl_data_source* source = nullptr;
        Buffer png;         // must outlive the source: send() can fire anytime
        bool cancelled = false;
    } gClip;

    void clipReg(void*, wl_registry* reg, u32 name, const char* iface, u32 ver) {
        if (StringView(iface) == "wl_seat"_sv) {
            gClip.seat = (wl_seat*)wl_registry_bind(reg, name, &wl_seat_interface, 1);
        } else if (StringView(iface) == "wl_data_device_manager"_sv) {
            gClip.ddm = (wl_data_device_manager*)wl_registry_bind(reg, name, &wl_data_device_manager_interface, ver < 3 ? ver : 3);
        }
    }

    const wl_registry_listener clipRegListener = {clipReg, [](void*, wl_registry*, u32) {}};

    void clipSend(void*, wl_data_source*, const char*, int32_t fd) {
        ScopedFD sfd(fd);

        try {
            FDPipe out(sfd);

            out.write(gClip.png.data(), gClip.png.length());
        } catch (...) {
            // the paste target may close its end early (EPIPE) — nothing to do
        }
    }

    void clipCancelled(void*, wl_data_source* src) {
        // another client took the selection — drop the source and let us exit
        wl_data_source_destroy(src);
        gClip.source = nullptr;
        gClip.cancelled = true;
    }

    const wl_data_source_listener clipSourceListener = {
        .target = [](void*, wl_data_source*, const char*) {},
        .send = clipSend,
        .cancelled = clipCancelled,
        .dnd_drop_performed = [](void*, wl_data_source*) {},
        .dnd_finished = [](void*, wl_data_source*) {},
        .action = [](void*, wl_data_source*, u32) {},
    };

    // put the encoded png (already in gClip.png) on the clipboard and block
    // servicing wayland events until the selection is lost. the window is
    // hidden by now, so we only hold the selection. throws if the compositor
    // exposes no data-device machinery.
    void copyToClipboard() {
        wl_display* dpy = glfwGetWaylandDisplay();
        wl_registry* reg = wl_display_get_registry(dpy);

        wl_registry_add_listener(reg, &clipRegListener, nullptr);
        wl_display_roundtrip(dpy); // receive the globals, then their binds

        if (!gClip.seat || !gClip.ddm) {
            fail("no wayland clipboard"_sv);
        }

        wl_data_device* dd = wl_data_device_manager_get_data_device(gClip.ddm, gClip.seat);

        gClip.source = wl_data_device_manager_create_data_source(gClip.ddm);
        wl_data_source_add_listener(gClip.source, &clipSourceListener, nullptr);
        wl_data_source_offer(gClip.source, "image/png");
        // our compositor ignores the set_selection serial, so 0 is fine
        wl_data_device_set_selection(dd, gClip.source, 0);
        wl_display_flush(dpy);

        // linger as the clipboard owner until another client takes over
        while (!gClip.cancelled) {
            if (wl_display_dispatch(dpy) < 0) {
                break; // compositor went away
            }
        }
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

    // ui scale handed down from the compositor via IMGUI_SCALE (its clients
    // otherwise render at scale 1, so the panel/text would be tiny on hidpi)
    float gUiScale = 1.f;

    void vkc(VkResult e) {
        if (e < 0) {
            fail(sv(StringBuilder() << "vulkan error "_sv << (i64)e));
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
            fail("no vulkan WSI support"_sv);
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
    // selection in image px; empty (zero-area) means "no selection", which the
    // save/copy path treats as the whole frame. drawn by a left-drag, wiped by
    // a pan or a zoom change
    struct Crop {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool dragging = false;
        float dragOx = 0, dragOy = 0;

        bool empty() const {
            return x1 - x0 < 1 || y1 - y0 < 1;
        }

        void clear() {
            x0 = y0 = x1 = y1 = 0;
            dragging = false;
        }
    };

    // view state: the on-screen zoom (percent, view-only — save/copy always use
    // full-res image px) plus the current selection
    constexpr int kInitialZoom = 50;

    struct Viewer {
        int zoom = kInitialZoom;
        Crop crop;
    };

    float clampf(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    constexpr int kZoomMin = 10, kZoomMax = 400, kZoomStep = 10;

    // nudge the zoom by delta% (clamped); any change drops the selection
    void applyZoom(Viewer& v, int delta) {
        int z = (int)clampf((float)(v.zoom + delta), (float)kZoomMin, (float)kZoomMax);

        if (z != v.zoom) {
            v.zoom = z;
            v.crop.clear();
        }
    }

    // left control panel: zoom on top, then Save/Copy/Exit in one row, then the
    // selection readout. writes the chosen action into result.
    void drawPanel(Viewer& v, int& result) {
        Crop& crop = v.crop;

        ImGui::TextUnformatted("Zoom (view only)");
        ImGui::SetNextItemWidth(-FLT_MIN);

        int z = v.zoom;

        if (ImGui::SliderInt("##zoom", &z, kZoomMin, kZoomMax, "%d%%")) {
            applyZoom(v, z - v.zoom); // clamps + drops the selection
        }

        ImGui::Spacing();

        // three equal buttons across the panel width
        float avail = ImGui::GetContentRegionAvail().x;
        float bw = (avail - ImGui::GetStyle().ItemSpacing.x * 2.f) / 3.f;

        if (ImGui::Button("Save", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            result = 1;
        }

        ImGui::SameLine();

        bool ctrlC = ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C);

        if (ImGui::Button("Copy", ImVec2(bw, 0)) || ctrlC) {
            result = 2;
        }

        ImGui::SameLine();

        if (ImGui::Button("Exit", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            result = -1;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (crop.empty()) {
            ImGui::TextDisabled("whole frame");
        } else {
            auto& text = sb();

            text << "selection "_sv << (i64)(crop.x1 - crop.x0 + 0.5f) << " x "_sv << (i64)(crop.y1 - crop.y0 + 0.5f);
            ImGui::TextUnformatted(text.cStr());
        }

        ImGui::TextDisabled("drag: select");
        ImGui::TextDisabled("middle-drag: pan");
        ImGui::TextDisabled("scroll / +-: zoom");
    }

    // right canvas: the image at v.zoom in a scrollable viewport. left-drag
    // draws the crop selection; middle-drag pans and wipes the selection.
    void drawCanvas(const Image& img, Texture& tex, Viewer& v) {
        Crop& crop = v.crop;
        float scale = (float)v.zoom / 100.f;
        ImVec2 content((float)img.w * scale, (float)img.h * scale);
        ImVec2 origin = ImGui::GetCursorScreenPos();

        // an invisible button both sizes the scroll region and captures the
        // left-drag for selection
        ImGui::InvisibleButton("img", content, ImGuiButtonFlags_MouseButtonLeft);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddImage((ImTextureID)tex.ds, origin, ImVec2(origin.x + content.x, origin.y + content.y));

        ImVec2 mouse = ImGui::GetIO().MousePos;
        auto toScreen = [&](float px, float py) {
            return ImVec2(origin.x + px * scale, origin.y + py * scale);
        };
        auto toImg = [&](ImVec2 s) {
            return ImVec2(clampf((s.x - origin.x) / scale, 0.f, (float)img.w), clampf((s.y - origin.y) / scale, 0.f, (float)img.h));
        };

        // middle-drag pans the viewport and clears the selection
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;

            ImGui::SetScrollX(ImGui::GetScrollX() - d.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - d.y);
            crop.clear();
        }

        // wheel zooms (the child has NoScrollWithMouse, so the wheel is ours)
        float wheel = ImGui::GetIO().MouseWheel;

        if (wheel != 0.f && ImGui::IsWindowHovered()) {
            applyZoom(v, wheel > 0.f ? kZoomStep : -kZoomStep);
        }

        // left-drag on the image draws a fresh selection
        if (ImGui::IsItemActivated()) {
            ImVec2 p = toImg(mouse);

            crop.dragOx = p.x;
            crop.dragOy = p.y;
            crop.dragging = true;
        }

        if (crop.dragging) {
            ImVec2 p = toImg(mouse);

            crop.x0 = crop.dragOx < p.x ? crop.dragOx : p.x;
            crop.y0 = crop.dragOy < p.y ? crop.dragOy : p.y;
            crop.x1 = crop.dragOx > p.x ? crop.dragOx : p.x;
            crop.y1 = crop.dragOy > p.y ? crop.dragOy : p.y;

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                crop.dragging = false;
            }
        }

        // dim outside the selection, outline it (clipped to the child)
        if (!crop.empty()) {
            ImVec2 s0 = toScreen(crop.x0, crop.y0);
            ImVec2 s1 = toScreen(crop.x1, crop.y1);
            ImVec2 hi(origin.x + content.x, origin.y + content.y);
            ImU32 dim = IM_COL32(0, 0, 0, 140);

            dl->AddRectFilled(origin, ImVec2(hi.x, s0.y), dim);              // above
            dl->AddRectFilled(ImVec2(origin.x, s1.y), hi, dim);             // below
            dl->AddRectFilled(ImVec2(origin.x, s0.y), s0, dim);            // left
            dl->AddRectFilled(ImVec2(s1.x, s0.y), ImVec2(hi.x, s1.y), dim); // right
            dl->AddRect(s0, s1, IM_COL32(255, 255, 255, 230), 0, 0, 1.5f);
        }
    }

    // draw the whole cropper; returns 1 = save, 2 = copy, -1 = cancel,
    // 0 = keep going
    int drawUi(const Image& img, Texture& tex, Viewer& v) {
        ImGuiViewport* vp = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);

        int result = 0;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        if (ImGui::Begin("##shot", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
            const float panelW = 200.f * gUiScale;

            // +/- zoom, handled before the panel so the slider reflects it
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
                applyZoom(v, kZoomStep);
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
                applyZoom(v, -kZoomStep);
            }

            if (ImGui::BeginChild("panel", ImVec2(panelW, 0), ImGuiChildFlags_Borders)) {
                drawPanel(v, result);
            }

            ImGui::EndChild();
            ImGui::SameLine();

            if (ImGui::BeginChild("canvas", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                drawCanvas(img, tex, v);
            }

            ImGui::EndChild();
        }

        ImGui::End();
        ImGui::PopStyleVar();

        return result;
    }

    // a full-window panel that replaces the cropper (not an overlay) when
    // something goes wrong — reads like a message from the compositor: a
    // heading, the error text, and a single Exit button. returns -1 on exit.
    int drawError(StringView msg) {
        ImGuiViewport* vp = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);

        int result = 0;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(28, 28, 32, 255));

        if (ImGui::Begin("##err", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
            float pad = 24.f * gUiScale;

            ImGui::SetCursorPos(ImVec2(pad, pad));
            ImGui::BeginGroup();
            ImGui::TextDisabled("imway screenshot");
            ImGui::Spacing();
            ImGui::PushTextWrapPos(vp->Size.x - pad);
            ImGui::TextUnformatted((const char*)msg.data(), (const char*)msg.data() + msg.length());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::Spacing();

            if (ImGui::Button("Exit", ImVec2(120.f * gUiScale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                result = -1;
            }

            ImGui::EndGroup();
        }

        ImGui::End();
        ImGui::PopStyleColor();

        return result;
    }

    // the vulkan/imgui frame loop: pump events, resize the swapchain on demand,
    // and call draw() each frame until it returns a nonzero action (window close
    // counts as exit). draw() is the cropper or the error panel.
    template <typename Draw>
    int runLoop(GLFWwindow* window, Draw draw) {
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

            action = draw();

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

        return action ? action : -1; // closing the window means exit
    }

    // the crop rect in image px, with the empty-selection-is-whole-frame rule
    void cropRegion(const Image& img, const Crop& c, int& x0, int& y0, int& x1, int& y1) {
        x0 = (int)(clampf(c.x0, 0, (float)img.w) + 0.5f);
        y0 = (int)(clampf(c.y0, 0, (float)img.h) + 0.5f);
        x1 = (int)(clampf(c.x1, 0, (float)img.w) + 0.5f);
        y1 = (int)(clampf(c.y1, 0, (float)img.h) + 0.5f);

        if (x1 - x0 < 1 || y1 - y0 < 1) {
            x0 = 0;
            y0 = 0;
            x1 = (int)img.w;
            y1 = (int)img.h;
        }
    }
}

int mainScreenshot(StringView path) {
    signal(SIGPIPE, SIG_IGN); // a paste target closing its pipe must not kill us

    if (const char* s = getenv("IMGUI_SCALE")) {
        double v = parseFloat(StringView(s));

        if (v > 0.0) {
            gUiScale = (float)v;
        }
    }

    // load first; any failure becomes an on-screen error panel, not a console
    // line, so it reads like a message from the compositor
    Image img;
    Buffer errText;
    bool loaded = false;

    try {
        loadImage(path, img);
        loaded = true;
    } catch (ShotError& e) {
        errText = Buffer(e.description());
    } catch (...) {
        errText = Buffer(Exception::current());
    }

    glfwSetErrorCallback([](int e, const char* d) {
        sysE << "imway screenshot: GLFW "_sv << (i64)e << ": "_sv << StringView(d) << endL;
    });

    if (!glfwInit()) {
        sysE << "imway screenshot: no display"_sv << endL;

        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Prepare the exact style that drawUi will use before sizing the native
    // window. drawUi holds WindowPadding at zero while creating both children,
    // so only SameLine's ItemSpacing separates the panel and image viewport.
    ImGuiStyle uiStyle;

    if (gUiScale != 1.f) {
        uiStyle.FontScaleMain = gUiScale;
        uiStyle.ScaleAllSizes(gUiScale);
    }

    // Open at the image's on-screen size (50% zoom) plus the actual ImGui
    // chrome. A bare error panel gets a small fixed size. Clamp to 90% of the
    // monitor.
    const float panelW = 200.f * gUiScale;
    int winW, winH;

    if (loaded) {
        float zoom = (float)kInitialZoom / 100.f;

        winW = (int)ceilf(panelW + uiStyle.ItemSpacing.x + img.w * zoom);
        winH = (int)ceilf(img.h * zoom);

        int minH = (int)(220.f * gUiScale);

        if (winH < minH) {
            winH = minH;
        }
    } else {
        winW = (int)(480.f * gUiScale);
        winH = (int)(180.f * gUiScale);
    }

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
        sysE << "imway screenshot: no vulkan window"_sv << endL;

        return 1;
    }

    int rc = 0;

    try {
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
        ImGui::GetStyle() = uiStyle;

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

        if (loaded) {
            uploadTexture(img, tex);
        }

        Viewer view; // zoom 50%, no selection (whole frame) until the user drags

        // interactive phase: the cropper, or the error panel if the load failed
        int action = runLoop(window, [&] {
            return errText.empty() ? drawUi(img, tex, view) : drawError(sv(errText));
        });

        // action phase: encode + save/copy; a failure switches to the error panel
        if (loaded && errText.empty() && (action == 1 || action == 2)) {
            try {
                int x0, y0, x1, y1;

                cropRegion(img, view.crop, x0, y0, x1, y1);

                if (action == 1) {
                    Buffer png;

                    encodePng(img, x0, y0, x1, y1, png);

                    Buffer dest = destPath();

                    savePng(png, sv(dest));
                    sysO << "imway screenshot: saved "_sv << sv(dest) << endL;
                } else {
                    encodePng(img, x0, y0, x1, y1, gClip.png);
                    // nothing left to show; hold the selection with no window
                    glfwHideWindow(window);
                    copyToClipboard();
                }
            } catch (ShotError& e) {
                errText = Buffer(e.description());
            } catch (...) {
                errText = Buffer(Exception::current());
            }

            if (!errText.empty()) {
                glfwShowWindow(window);
                runLoop(window, [&] { return drawError(sv(errText)); });
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
    } catch (...) {
        // vulkan/imgui setup blew up — nothing to show it on; log and leave the
        // rest for the OS to reclaim on exit
        sysE << "imway screenshot: "_sv << Exception::current() << endL;
        rc = 1;
    }

    glfwTerminate();

    return rc;
}
