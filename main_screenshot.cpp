#include "main_screenshot.h"
#include "color.h"
#include "util.h"

#include <fcntl.h>
#include <time.h>
#include <float.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <std/ios/sys.h>
#include <std/ios/out_fd.h>
#include <std/ios/fs_utils.h>
#include <std/lib/vector.h>
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

#include <screenshot_pq.spv.h>

#include <png.h>
#include <jxl/color_encoding.h>
#include <jxl/encode.h>

using namespace stl;

// imway screenshot <path>: a standalone GLFW+Vulkan imgui client. On KMS the
// path names an owned scanout DMA-BUF and metadata comes through the
// environment; headless uses a self-describing IMW1 memfd.  The viewer samples
// the shared image directly and reads back only the selected region on
// Save/Copy. GLFW owns window/input and imgui drives the Vulkan UI.

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
        int dmaFd = -1;
        u32 format = 0;
        u32 offset = 0;
        u32 stride = 0;
        u64 modifier = 0;
        u64 allocationSize = 0;
        u64 renderDevice = 0;
        bool dmabuf = false;
        OutputColorState color;
        Buffer rgb16;

        bool shared() const;
    };

    struct Texture;
    void readTexture(const Image& img, const Texture& tex, int x0, int y0,
                     int x1, int y1, Image& out);

    constexpr u32 kMagic = 0x31574d49u; // 'IMW1' little-endian

    bool Image::shared() const {
        return dmabuf;
    }

    bool parseShared(StringView spec, Image& img) {
        u64 values[8] = {};
        size_t pos = 0;

        for (int i = 0; i < 8; i++) {
            size_t begin = pos;

            while (pos < spec.length() && spec[pos] >= '0' &&
                   spec[pos] <= '9') {
                u64 digit = (u64)(spec[pos] - '0');

                if (values[i] > (UINT64_MAX - digit) / 10) {
                    return false;
                }

                values[i] = values[i] * 10 + digit;
                pos++;
            }

            if (pos == begin ||
                (i < 7 ? pos >= spec.length() || spec[pos] != ':' :
                         pos != spec.length())) {
                return false;
            }

            pos++;
        }

        img.w = (u32)values[0];
        img.h = (u32)values[1];
        img.format = (u32)values[2];
        img.offset = (u32)values[3];
        img.stride = (u32)values[4];
        img.modifier = values[5];
        img.allocationSize = values[6];
        img.renderDevice = values[7];

        return img.w && img.h && img.stride && img.allocationSize;
    }

    // throws (ShotError, or the Errno readFileContent raises) on any failure
    void loadImage(StringView path, Image& img) {
        Buffer p(path);

        if (const char* color = getenv("IMWAY_SHOT_COLOR")) {
            StringView value(color), hs, ns;

            if (value.split(':', hs, ns)) {
                double white = parseFloat(ns);

                img.color = hs == "1"_sv ? OutputColorState::hdr10(white) :
                                             OutputColorState::sdr();
            }
        }

        if (const char* spec = getenv("IMWAY_SHOT_DMABUF")) {
            if (!parseShared(StringView(spec), img)) {
                fail("bad shared screenshot metadata"_sv);
            }

            img.dmaFd = open(p.cStr(), O_RDWR | O_CLOEXEC);

            if (img.dmaFd < 0) {
                fail("cannot open shared screenshot"_sv);
            }

            img.dmabuf = true;

            return;
        }

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

    // ---- encoded output ----
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
    void saveFile(const Buffer& data, StringView file) {
        ScopedFD fd(open(Buffer(file).cStr(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));

        if (fd.get() < 0) {
            fail(sv(StringBuilder() << "cannot write "_sv << file));
        }

        FDRegular out(fd);

        out.write(data.data(), data.length());
        out.flush();
    }

    void encodeJxlPixels(const Image& img, const u16* pixels, u32 w, u32 h,
                         Buffer& out) {
        JxlEncoder* enc = JxlEncoderCreate(nullptr);

        if (!enc) {
            fail("jxl encoder allocation failed"_sv);
        }

        JxlBasicInfo info;

        JxlEncoderInitBasicInfo(&info);
        info.xsize = w;
        info.ysize = h;
        info.bits_per_sample = 16;
        info.num_color_channels = 3;
        info.uses_original_profile = JXL_TRUE;

        JxlColorEncoding color{};

        if (img.color.hdr()) {
            color.color_space = JXL_COLOR_SPACE_RGB;
            color.white_point = JXL_WHITE_POINT_D65;
            color.primaries = JXL_PRIMARIES_2100;
            color.transfer_function = JXL_TRANSFER_FUNCTION_PQ;
            color.rendering_intent = JXL_RENDERING_INTENT_RELATIVE;
        } else {
            JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
        }

        JxlEncoderFrameSettings* frame = JxlEncoderFrameSettingsCreate(enc, nullptr);
        JxlPixelFormat format{3, JXL_TYPE_UINT16, JXL_NATIVE_ENDIAN, 0};
        size_t bytes = (size_t)w * h * 3 * sizeof(u16);
        bool ok = JxlEncoderSetBasicInfo(enc, &info) == JXL_ENC_SUCCESS &&
                  JxlEncoderSetColorEncoding(enc, &color) == JXL_ENC_SUCCESS &&
                  frame && JxlEncoderSetFrameLossless(frame, JXL_TRUE) == JXL_ENC_SUCCESS &&
                  JxlEncoderAddImageFrame(frame, &format, pixels, bytes) == JXL_ENC_SUCCESS;

        if (!ok) {
            JxlEncoderDestroy(enc);
            fail("jxl encode setup failed"_sv);
        }

        JxlEncoderCloseInput(enc);
        out.reset();

        for (;;) {
            unsigned char chunk[64 * 1024];
            unsigned char* next = chunk;
            size_t available = sizeof(chunk);
            JxlEncoderStatus status = JxlEncoderProcessOutput(enc, &next, &available);

            out.append(chunk, sizeof(chunk) - available);

            if (status == JXL_ENC_SUCCESS) {
                break;
            }
            if (status != JXL_ENC_NEED_MORE_OUTPUT) {
                JxlEncoderDestroy(enc);
                fail("jxl encode failed"_sv);
            }
        }

        JxlEncoderDestroy(enc);
    }

    void encodeJxlSelection(const Image& img, const Texture& tex, int x0,
                            int y0, int x1, int y1, Buffer& out) {
        Image selected;

        if (img.shared()) {
            readTexture(img, tex, x0, y0, x1, y1, selected);
        } else {
            selected.w = (u32)(x1 - x0);
            selected.h = (u32)(y1 - y0);
            selected.rgb16.zero((size_t)selected.w * selected.h * 3 * sizeof(u16));
            u16* dst = (u16*)selected.rgb16.mutData();

            for (int y = y0; y < y1; y++) {
                for (int x = x0; x < x1; x++) {
                    const u8* src = img.px + ((size_t)y * img.w + x) * 4;
                    size_t at = ((size_t)(y - y0) * selected.w + (x - x0)) * 3;

                    dst[at + 0] = (u16)(src[0] * 257);
                    dst[at + 1] = (u16)(src[1] * 257);
                    dst[at + 2] = (u16)(src[2] * 257);
                }
            }
        }

        selected.color = img.color;
        encodeJxlPixels(selected, (const u16*)selected.rgb16.data(),
                        selected.w, selected.h, out);
    }

    // $XDG_PICTURES_DIR/screenshots/imway-YYYYMMDD-HHMMSS.jxl (fallback ~/Pictures)
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

        return Buffer(sv(StringBuilder() << sv(dir) << "/imway-"_sv << StringView(stamp) << ".jxl"_sv));
    }

    // ---- wayland clipboard ----
    // glfw only speaks the text clipboard, so image/jxl and the compatibility
    // image/png go on the Wayland selection directly. Bind wl_seat and
    // wl_data_device_manager off GLFW's display, then keep the source alive
    // until the selection is taken over — the same contract wl-copy honors.
    struct Clip {
        wl_seat* seat = nullptr;
        wl_pointer* pointer = nullptr;
        wl_keyboard* keyboard = nullptr;
        wl_data_device_manager* ddm = nullptr;
        wl_data_device* device = nullptr;
        wl_data_source* source = nullptr;
        Buffer png;         // must outlive the source: send() can fire anytime
        Buffer jxl;
        u32 serial = 0;
        bool cancelled = false;
    } gClip;

    void clipPointerEnter(void*, wl_pointer*, u32 serial, wl_surface*, wl_fixed_t, wl_fixed_t) {
        gClip.serial = serial;
    }

    void clipPointerLeave(void*, wl_pointer*, u32, wl_surface*) {
    }

    void clipPointerMotion(void*, wl_pointer*, u32, wl_fixed_t, wl_fixed_t) {
    }

    void clipPointerButton(void*, wl_pointer*, u32 serial, u32, u32, u32) {
        gClip.serial = serial;
    }

    void clipPointerAxis(void*, wl_pointer*, u32, u32, wl_fixed_t) {
    }

    void clipPointerFrame(void*, wl_pointer*) {
    }

    void clipPointerAxisSource(void*, wl_pointer*, u32) {
    }

    void clipPointerAxisStop(void*, wl_pointer*, u32, u32) {
    }

    void clipPointerAxisDiscrete(void*, wl_pointer*, u32, i32) {
    }

    const wl_pointer_listener clipPointerListener = {
        .enter = clipPointerEnter,
        .leave = clipPointerLeave,
        .motion = clipPointerMotion,
        .button = clipPointerButton,
        .axis = clipPointerAxis,
        .frame = clipPointerFrame,
        .axis_source = clipPointerAxisSource,
        .axis_stop = clipPointerAxisStop,
        .axis_discrete = clipPointerAxisDiscrete,
    };

    void clipKeyboardKeymap(void*, wl_keyboard*, u32, i32 fd, u32) {
        close(fd);
    }

    void clipKeyboardEnter(void*, wl_keyboard*, u32 serial, wl_surface*, wl_array*) {
        gClip.serial = serial;
    }

    void clipKeyboardLeave(void*, wl_keyboard*, u32, wl_surface*) {
    }

    void clipKeyboardKey(void*, wl_keyboard*, u32 serial, u32, u32, u32) {
        gClip.serial = serial;
    }

    void clipKeyboardModifiers(void*, wl_keyboard*, u32, u32, u32, u32, u32) {
    }

    void clipKeyboardRepeatInfo(void*, wl_keyboard*, i32, i32) {
    }

    const wl_keyboard_listener clipKeyboardListener = {
        .keymap = clipKeyboardKeymap,
        .enter = clipKeyboardEnter,
        .leave = clipKeyboardLeave,
        .key = clipKeyboardKey,
        .modifiers = clipKeyboardModifiers,
        .repeat_info = clipKeyboardRepeatInfo,
    };

    void clipSeatCapabilities(void*, wl_seat* seat, u32 caps) {
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !gClip.pointer) {
            gClip.pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(gClip.pointer, &clipPointerListener, nullptr);
        }

        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !gClip.keyboard) {
            gClip.keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(gClip.keyboard, &clipKeyboardListener, nullptr);
        }
    }

    void clipSeatName(void*, wl_seat*, const char*) {
    }

    const wl_seat_listener clipSeatListener = {
        .capabilities = clipSeatCapabilities,
        .name = clipSeatName,
    };

    void clipReg(void*, wl_registry* reg, u32 name, const char* iface, u32 ver) {
        if (StringView(iface) == "wl_seat"_sv) {
            gClip.seat = (wl_seat*)wl_registry_bind(reg, name, &wl_seat_interface, ver < 5 ? ver : 5);
            wl_seat_add_listener(gClip.seat, &clipSeatListener, nullptr);
        } else if (StringView(iface) == "wl_data_device_manager"_sv) {
            gClip.ddm = (wl_data_device_manager*)wl_registry_bind(reg, name, &wl_data_device_manager_interface, ver < 3 ? ver : 3);
        }
    }

    const wl_registry_listener clipRegListener = {clipReg, [](void*, wl_registry*, u32) {}};

    void clipSend(void*, wl_data_source*, const char* mime, int32_t fd) {
        ScopedFD sfd(fd);

        try {
            FDPipe out(sfd);

            const Buffer& data = StringView(mime) == "image/jxl"_sv ? gClip.jxl : gClip.png;

            out.write(data.data(), data.length());
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

    void initClipboard() {
        wl_display* dpy = glfwGetWaylandDisplay();
        wl_registry* reg = wl_display_get_registry(dpy);

        wl_registry_add_listener(reg, &clipRegListener, nullptr);
        wl_display_roundtrip(dpy);
        wl_display_roundtrip(dpy);
        wl_registry_destroy(reg);

        if (gClip.seat && gClip.ddm) {
            gClip.device = wl_data_device_manager_get_data_device(gClip.ddm, gClip.seat);
            wl_display_roundtrip(dpy);
        }
    }

    // Put the encoded JXL and PNG fallback on the clipboard and service
    // Wayland events until ownership is lost. The window is hidden by now.
    void copyToClipboard(GLFWwindow* window) {
        wl_display* dpy = glfwGetWaylandDisplay();

        if (!gClip.device || !gClip.serial) {
            fail("no wayland clipboard"_sv);
        }

        gClip.source = wl_data_device_manager_create_data_source(gClip.ddm);
        wl_data_source_add_listener(gClip.source, &clipSourceListener, nullptr);
        wl_data_source_offer(gClip.source, "image/jxl");
        wl_data_source_offer(gClip.source, "image/png");
        gClip.cancelled = false;
        wl_data_device_set_selection(gClip.device, gClip.source, gClip.serial);
        wl_display_flush(dpy);

        // Selection serials are accepted only while their client owns keyboard
        // focus. Publish first, then hide and hand focus to the paste target.
        glfwHideWindow(window);

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

    bool hasDeviceExtension(VkPhysicalDevice device, const char* name) {
        u32 count = 0;

        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
        Vector<VkExtensionProperties> props;

        props.zero(count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                             props.mutData());

        for (const VkExtensionProperties& prop : props) {
            if (StringView(prop.extensionName) == StringView(name)) {
                return true;
            }
        }

        return false;
    }

    void setupVulkan(const char** exts, u32 nexts, const Image& img) {
        VkApplicationInfo app = {};

        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "imway screenshot";
        app.apiVersion = VK_API_VERSION_1_2;

        Vector<const char*> instanceExts;

        for (u32 i = 0; i < nexts; i++) {
            instanceExts.pushBack(exts[i]);
        }

        if (img.color.hdr()) {
            instanceExts.pushBack(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
        }

        VkInstanceCreateInfo ci = {};

        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = (u32)instanceExts.length();
        ci.ppEnabledExtensionNames = instanceExts.data();
        vkc(vkCreateInstance(&ci, gAlloc, &gInstance));

        if (img.shared()) {
            u32 count = 0;

            vkEnumeratePhysicalDevices(gInstance, &count, nullptr);
            Vector<VkPhysicalDevice> devices;

            devices.zero(count);
            vkEnumeratePhysicalDevices(gInstance, &count, devices.mutData());

            for (VkPhysicalDevice device : devices) {
                if (!hasDeviceExtension(
                        device, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) {
                    continue;
                }

                VkPhysicalDeviceDrmPropertiesEXT drm{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
                VkPhysicalDeviceProperties2 props{
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};

                props.pNext = &drm;
                vkGetPhysicalDeviceProperties2(device, &props);

                bool render = drm.hasRender &&
                    (u64)makedev((u32)drm.renderMajor,
                                 (u32)drm.renderMinor) == img.renderDevice;
                bool primary = drm.hasPrimary &&
                    (u64)makedev((u32)drm.primaryMajor,
                                 (u32)drm.primaryMinor) == img.renderDevice;

                if (render || primary) {
                    gPhys = device;

                    break;
                }
            }

            if (!gPhys) {
                fail("shared screenshot gpu is unavailable"_sv);
            }
        } else {
            gPhys = ImGui_ImplVulkanH_SelectPhysicalDevice(gInstance);
        }

        gQueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(gPhys);

        const char* devExts[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
            VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
        };
        u32 devExtCount = img.shared() ? 4 : 1;

        for (u32 i = 0; i < devExtCount; i++) {
            if (!hasDeviceExtension(gPhys, devExts[i])) {
                fail("vulkan cannot import shared screenshot"_sv);
            }
        }
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
        dci.enabledExtensionCount = devExtCount;
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

    void setupVulkanWindow(VkSurfaceKHR surface, int w, int h, bool hdr) {
        ImGui_ImplVulkanH_Window* wd = &gWin;

        wd->Surface = surface;

        VkBool32 sup = VK_FALSE;

        vkGetPhysicalDeviceSurfaceSupportKHR(gPhys, gQueueFamily, surface, &sup);

        if (!sup) {
            fail("no vulkan WSI support"_sv);
        }

        const VkFormat hdrFmts[] = {
            VK_FORMAT_A2R10G10B10_UNORM_PACK32,
            VK_FORMAT_A2B10G10R10_UNORM_PACK32,
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
        };
        const VkFormat sdrFmts[] = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8_UNORM,
            VK_FORMAT_R8G8B8_UNORM,
        };
        const VkFormat* fmts = hdr ? hdrFmts : sdrFmts;

        VkColorSpaceKHR colorSpace = hdr ? VK_COLOR_SPACE_HDR10_ST2084_EXT :
                                          VK_COLORSPACE_SRGB_NONLINEAR_KHR;

        wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            gPhys, surface, fmts, 4, colorSpace);

        if (hdr && wd->SurfaceFormat.colorSpace != colorSpace) {
            fail("vulkan WSI has no BT.2020/PQ surface"_sv);
        }

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

    void finishTexture(const Image& img, Texture& tex) {
        VkImageViewCreateInfo vci = {};

        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = tex.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = img.shared() ? (VkFormat)img.format :
                                    VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkc(vkCreateImageView(gDevice, &vci, gAlloc, &tex.view));

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
        vkc(vkCreateSampler(gDevice, &sci, gAlloc, &tex.sampler));

        tex.ds = ImGui_ImplVulkan_AddTexture(
            tex.sampler, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void importTexture(Image& img, Texture& tex) {
        VkSubresourceLayout plane = {};

        plane.offset = img.offset;
        plane.rowPitch = img.stride;

        VkImageDrmFormatModifierExplicitCreateInfoEXT modifier = {};

        modifier.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
        modifier.drmFormatModifier = img.modifier;
        modifier.drmFormatModifierPlaneCount = 1;
        modifier.pPlaneLayouts = &plane;

        VkExternalMemoryImageCreateInfo external = {};

        external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external.pNext = &modifier;
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

        VkImageCreateInfo ici = {};

        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.pNext = &external;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = (VkFormat)img.format;
        ici.extent = {img.w, img.h, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkc(vkCreateImage(gDevice, &ici, gAlloc, &tex.image));

        VkMemoryRequirements req = {};

        vkGetImageMemoryRequirements(gDevice, tex.image, &req);

        auto getFdProps = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(
            gDevice, "vkGetMemoryFdPropertiesKHR");
        VkMemoryFdPropertiesKHR fdProps{
            VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};

        if (!getFdProps ||
            getFdProps(gDevice, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                       img.dmaFd, &fdProps) != VK_SUCCESS) {
            fail("cannot query shared screenshot memory"_sv);
        }

        u32 memoryTypes = req.memoryTypeBits & fdProps.memoryTypeBits;

        if (!memoryTypes) {
            fail("shared screenshot memory is incompatible"_sv);
        }

        VkMemoryDedicatedAllocateInfo dedicated{
            VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};

        dedicated.image = tex.image;

        VkImportMemoryFdInfoKHR import{
            VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};

        import.pNext = &dedicated;
        import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        import.fd = img.dmaFd;

        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

        mai.pNext = &import;
        mai.allocationSize = img.allocationSize;
        mai.memoryTypeIndex = findMemoryType(memoryTypes, 0);
        vkc(vkAllocateMemory(gDevice, &mai, gAlloc, &tex.memory));
        img.dmaFd = -1;
        vkc(vkBindImageMemory(gDevice, tex.image, tex.memory, 0));

        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};

        pci.queueFamilyIndex = gQueueFamily;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkc(vkCreateCommandPool(gDevice, &pci, gAlloc, &pool));

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cai{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        vkc(vkAllocateCommandBuffers(gDevice, &cai, &cmd));

        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkc(vkBeginCommandBuffer(cmd, &begin));

        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        barrier.dstQueueFamilyIndex = gQueueFamily;
        barrier.image = tex.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
        vkc(vkEndCommandBuffer(cmd));

        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};

        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkc(vkQueueSubmit(gQueue, 1, &submit, VK_NULL_HANDLE));
        vkc(vkQueueWaitIdle(gQueue));
        vkDestroyCommandPool(gDevice, pool, gAlloc);
        finishTexture(img, tex);
    }

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

        finishTexture(img, tex);
    }

    void readTexture(const Image& img, const Texture& tex, int x0, int y0,
                     int x1, int y1, Image& out) {
        out.w = (u32)(x1 - x0);
        out.h = (u32)(y1 - y0);

        VkDeviceSize bytes = (VkDeviceSize)out.w * out.h * sizeof(u32);
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};

        bci.size = bytes;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkc(vkCreateBuffer(gDevice, &bci, gAlloc, &buffer));

        VkMemoryRequirements req = {};

        vkGetBufferMemoryRequirements(gDevice, buffer, &req);

        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

        mai.allocationSize = req.size;
        mai.memoryTypeIndex = findMemoryType(
            req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkc(vkAllocateMemory(gDevice, &mai, gAlloc, &memory));
        vkc(vkBindBufferMemory(gDevice, buffer, memory, 0));

        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};

        pci.queueFamilyIndex = gQueueFamily;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkc(vkCreateCommandPool(gDevice, &pci, gAlloc, &pool));

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cai{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        vkc(vkAllocateCommandBuffers(gDevice, &cai, &cmd));

        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkc(vkBeginCommandBuffer(cmd, &begin));

        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);

        VkBufferImageCopy copy = {};

        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {x0, y0, 0};
        copy.imageExtent = {out.w, out.h, 1};
        vkCmdCopyImageToBuffer(cmd, tex.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                               &copy);

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
        vkc(vkEndCommandBuffer(cmd));

        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};

        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkc(vkQueueSubmit(gQueue, 1, &submit, VK_NULL_HANDLE));
        vkc(vkQueueWaitIdle(gQueue));

        void* map = nullptr;

        vkc(vkMapMemory(gDevice, memory, 0, bytes, 0, &map));
        out.file.zero((size_t)bytes);
        out.rgb16.zero((size_t)out.w * out.h * 3 * sizeof(u16));

        const u32* source = (const u32*)map;
        u32* dest = (u32*)out.file.mutData();
        u16* rgb16 = (u16*)out.rgb16.mutData();

        for (size_t i = 0; i < (size_t)out.w * out.h; i++) {
            u32 pixel = source[i];

            if ((VkFormat)img.format ==
                VK_FORMAT_A2R10G10B10_UNORM_PACK32) {
                u32 r = (pixel >> 20) & 1023;
                u32 g = (pixel >> 10) & 1023;
                u32 b = pixel & 1023;

                dest[i] = (r >> 2) | ((g >> 2) << 8) | ((b >> 2) << 16) | 0xff000000u;
                rgb16[i * 3 + 0] = (u16)((r * 65535 + 511) / 1023);
                rgb16[i * 3 + 1] = (u16)((g * 65535 + 511) / 1023);
                rgb16[i * 3 + 2] = (u16)((b * 65535 + 511) / 1023);
            } else {
                u32 r = (pixel >> 16) & 0xff;
                u32 g = (pixel >> 8) & 0xff;
                u32 b = pixel & 0xff;

                dest[i] = r | (g << 8) | (b << 16) | 0xff000000u;
                rgb16[i * 3 + 0] = (u16)(r * 257);
                rgb16[i * 3 + 1] = (u16)(g * 257);
                rgb16[i * 3 + 2] = (u16)(b * 257);
            }
        }

        out.px = (const u8*)out.file.data();
        vkUnmapMemory(gDevice, memory);
        vkDestroyCommandPool(gDevice, pool, gAlloc);
        vkDestroyBuffer(gDevice, buffer, gAlloc);
        vkFreeMemory(gDevice, memory, gAlloc);
    }

    void encodeSelection(const Image& img, const Texture& tex, int x0, int y0,
                         int x1, int y1, Buffer& png) {
        if (!img.shared()) {
            encodePng(img, x0, y0, x1, y1, png);

            return;
        }

        Image pixels;

        readTexture(img, tex, x0, y0, x1, y1, pixels);
        encodePng(pixels, 0, 0, (int)pixels.w, (int)pixels.h, png);
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

    void clampWindowSize(int& w, int& h) {
        if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
            if (const GLFWvidmode* vm = glfwGetVideoMode(mon)) {
                int maxW = vm->width * 9 / 10;
                int maxH = vm->height * 9 / 10;

                if (w > maxW) {
                    w = maxW;
                }

                if (h > maxH) {
                    h = maxH;
                }
            }
        }
    }

    void initialWindowSize(const Image& img, const ImGuiStyle& style, int& w, int& h) {
        float zoom = (float)kInitialZoom / 100.f;

        w = (int)ceilf(200.f * gUiScale + style.ItemSpacing.x + img.w * zoom);
        h = (int)ceilf(img.h * zoom);

        int minH = (int)(220.f * gUiScale);

        if (h < minH) {
            h = minH;
        }

        clampWindowSize(w, h);
    }

    // nudge the zoom by delta% (clamped); any change drops the selection
    void applyZoom(Viewer& v, int delta) {
        int z = (int)clampf((float)(v.zoom + delta), (float)kZoomMin, (float)kZoomMax);

        if (z != v.zoom) {
            v.zoom = z;
            v.crop.clear();
        }
    }

    void resetView(Viewer& v) {
        v.zoom = kInitialZoom;
        v.crop.clear();
    }

    // left control panel: zoom on top, then Save/Copy/Reset in one row, then
    // the selection readout. writes the chosen action into result.
    void drawPanel(Viewer& v, int& result, bool& reset) {
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

        if (ImGui::Button("Reset", ImVec2(bw, 0))) {
            resetView(v);
            reset = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
        ImGui::TextDisabled("scroll / +-: zoom, 0: reset");
    }

    // right canvas: the image at v.zoom in a scrollable viewport. left-drag
    // draws the crop selection; middle-drag pans and wipes the selection.
    void drawCanvas(const Image& img, Texture& tex, Viewer& v, bool reset) {
        Crop& crop = v.crop;

        if (reset) {
            ImGui::SetScrollX(0.f);
            ImGui::SetScrollY(0.f);
        }

        float scale = (float)v.zoom / 100.f;
        ImVec2 content((float)img.w * scale, (float)img.h * scale);
        ImVec2 origin = ImGui::GetCursorScreenPos();

        // an invisible button both sizes the scroll region and captures the
        // left-drag for selection
        ImGui::InvisibleButton("img", content, ImGuiButtonFlags_MouseButtonLeft);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (img.color.hdr()) {
            dl->AddCallback(ImGui_ImplVulkan_TextureEncodingCallback,
                            (void*)(intptr_t)2);
        }

        dl->AddImage((ImTextureID)tex.ds, origin,
                     ImVec2(origin.x + content.x, origin.y + content.y));

        if (img.color.hdr()) {
            dl->AddCallback(ImGui_ImplVulkan_TextureEncodingCallback, nullptr);
        }

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
    int drawUi(GLFWwindow* window, const Image& img, Texture& tex, Viewer& v) {
        ImGuiViewport* vp = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);

        int result = 0;
        bool reset = false;

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

            if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0)) {
                resetView(v);
                reset = true;
            }

            if (ImGui::BeginChild("panel", ImVec2(panelW, 0), ImGuiChildFlags_Borders)) {
                drawPanel(v, result, reset);
            }

            ImGui::EndChild();
            ImGui::SameLine();

            if (ImGui::BeginChild("canvas", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                drawCanvas(img, tex, v, reset);
            }

            ImGui::EndChild();
        }

        ImGui::End();
        ImGui::PopStyleVar();

        if (reset) {
            int w, h;

            initialWindowSize(img, ImGui::GetStyle(), w, h);
            glfwSetWindowSize(window, w, h);
        }

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
    int winW, winH;

    if (loaded) {
        initialWindowSize(img, uiStyle, winW, winH);
    } else {
        winW = (int)(480.f * gUiScale);
        winH = (int)(180.f * gUiScale);

        clampWindowSize(winW, winH);
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

        setupVulkan(exts, nexts, img);

        VkSurfaceKHR surface;

        vkc(glfwCreateWindowSurface(gInstance, window, gAlloc, &surface));

        int fbw, fbh;

        glfwGetFramebufferSize(window, &fbw, &fbh);
        setupVulkanWindow(surface, fbw, fbh, loaded && img.color.hdr());

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

        if (loaded && img.color.hdr()) {
            ii.CustomShaderFragCreateInfo.sType =
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            ii.CustomShaderFragCreateInfo.codeSize = sizeof(screenshot_pq_spv);
            ii.CustomShaderFragCreateInfo.pCode = screenshot_pq_spv;
        }

        ImGui_ImplVulkan_Init(&ii);
        ImGui_ImplVulkan_SetSdrWhite(
            img.color.hdr() ? (float)img.color.sdrWhiteNits : 203.f);
        initClipboard();

        Texture tex;

        if (loaded) {
            if (img.shared()) {
                importTexture(img, tex);
            } else {
                uploadTexture(img, tex);
            }
        }

        Viewer view; // zoom 50%, no selection (whole frame) until the user drags

        // interactive phase: the cropper, or the error panel if the load failed
        int action = runLoop(window, [&] {
            return errText.empty() ? drawUi(window, img, tex, view) : drawError(sv(errText));
        });

        // action phase: encode + save/copy; a failure switches to the error panel
        if (loaded && errText.empty() && (action == 1 || action == 2)) {
            try {
                int x0, y0, x1, y1;

                cropRegion(img, view.crop, x0, y0, x1, y1);

                if (action == 1) {
                    Buffer jxl;

                    encodeJxlSelection(img, tex, x0, y0, x1, y1, jxl);

                    Buffer dest = destPath();

                    saveFile(jxl, sv(dest));
                    sysO << "imway screenshot: saved "_sv << sv(dest) << endL;
                } else {
                    encodeJxlSelection(img, tex, x0, y0, x1, y1, gClip.jxl);
                    encodeSelection(img, tex, x0, y0, x1, y1, gClip.png);
                    // nothing left to show; hold the selection with no window
                    copyToClipboard(window);
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
        if (tex.ds) {
            ImGui_ImplVulkan_RemoveTexture(tex.ds);
        }
        if (tex.sampler) {
            vkDestroySampler(gDevice, tex.sampler, gAlloc);
        }
        if (tex.view) {
            vkDestroyImageView(gDevice, tex.view, gAlloc);
        }
        if (tex.image) {
            vkDestroyImage(gDevice, tex.image, gAlloc);
        }
        if (tex.memory) {
            vkFreeMemory(gDevice, tex.memory, gAlloc);
        }
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

    if (img.dmaFd >= 0) {
        close(img.dmaFd);
    }

    return rc;
}
