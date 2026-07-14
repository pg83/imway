#include "device.h"

#include "device_vk.h"
#include "output.h"
#include "renderer.h"
#include "scene.h"
#include "session.h"
#include "util.h"

#include <ev.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux/kd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

using namespace stl;

#define VK_CHECK(x) STD_VERIFY((x) == VK_SUCCESS)

namespace {
    struct ModeSpec {
        int w = 0, h = 0;
        double hz = 0;
    };

    bool parseModeSpec(const char* s, ModeSpec& m) {
        StringView v(s);
        StringView wh = v, hz;

        v.split('@', wh, hz);

        StringView ws, hs;

        if (!wh.split('x', ws, hs) || ws.empty() || hs.empty()) {
            return false;
        }

        m.w = (int)ws.stou();
        m.h = (int)hs.stou();
        m.hz = hz.empty() ? 0 : (double)hz.stou();

        return m.w > 0 && m.h > 0;
    }

    void connectorName(const drmModeConnector* c, char* buf, size_t len) {
        const char* t = drmModeGetConnectorTypeName(c->connector_type);

        snprintf(buf, len, "%s-%u", t ? t : "Unknown", c->connector_type_id);
    }

    void initVulkan(DeviceVk& vk, int drmFd) {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};

        app.pApplicationName = "imway";
        app.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};

        instInfo.pApplicationInfo = &app;
        VK_CHECK(vkCreateInstance(&instInfo, nullptr, &vk.instance));

        u32 n = 0;

        vkEnumeratePhysicalDevices(vk.instance, &n, nullptr);
        STD_VERIFY(n > 0);

        Vector<VkPhysicalDevice> devs;

        devs.zero(n);
        vkEnumeratePhysicalDevices(vk.instance, &n, devs.mutData());

        auto hasExt = [](VkPhysicalDevice d, const char* name) {
            u32 en = 0;

            vkEnumerateDeviceExtensionProperties(d, nullptr, &en, nullptr);

            Vector<VkExtensionProperties> eprops;

            eprops.zero(en);
            vkEnumerateDeviceExtensionProperties(d, nullptr, &en, eprops.mutData());

            for (const auto& e : eprops) {
                if (!strcmp(e.extensionName, name)) {
                    return true;
                }
            }

            return false;
        };

        vk.phys = VK_NULL_HANDLE;

        if (drmFd >= 0) {
            struct stat st{};

            if (fstat(drmFd, &st) == 0) {
                for (VkPhysicalDevice d : devs) {
                    if (!hasExt(d, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) {
                        continue;
                    }

                    VkPhysicalDeviceDrmPropertiesEXT drm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
                    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};

                    p2.pNext = &drm;
                    vkGetPhysicalDeviceProperties2(d, &p2);

                    bool primaryMatch = drm.hasPrimary && drm.primaryMajor == (i64)major(st.st_rdev) && drm.primaryMinor == (i64)minor(st.st_rdev);
                    bool renderMatch = drm.hasRender && drm.renderMajor == (i64)major(st.st_rdev) && drm.renderMinor == (i64)minor(st.st_rdev);

                    if (primaryMatch || renderMatch) {
                        vk.phys = d;

                        break;
                    }
                }
            }
        }

        if (vk.phys == VK_NULL_HANDLE) {
            vk.phys = devs[0];

            if (drmFd >= 0) {
                sysO << "imway: no vulkan device matches the drm node, render/display are split (readback path)"_sv << endL;
            }
        }

        VkPhysicalDeviceProperties props{};

        vkGetPhysicalDeviceProperties(vk.phys, &props);
        sysO << "imway: vulkan device: "_sv << (const char*)props.deviceName << endL;

        u32 qn = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(vk.phys, &qn, nullptr);

        Vector<VkQueueFamilyProperties> qf;

        qf.zero(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(vk.phys, &qn, qf.mutData());
        vk.queueFamily = UINT32_MAX;

        for (u32 i = 0; i < qn; i++) {
            if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                vk.queueFamily = i;

                break;
            }
        }

        STD_VERIFY(vk.queueFamily != UINT32_MAX);

        Vector<const char*> devExts;
        const char* need[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME};

        vk.hasDmabuf = true;

        for (const char* name : need) {
            if (!hasExt(vk.phys, name)) {
                vk.hasDmabuf = false;
                sysE << "imway: vulkan lacks "_sv << name << ", dmabuf disabled"_sv << endL;
            }
        }

        if (vk.hasDmabuf) {
            for (const char* name : need) {
                devExts.pushBack(name);
            }

            if (hasExt(vk.phys, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)) {
                devExts.pushBack(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
            }
        }

        float prio = 1.f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};

        qci.queueFamilyIndex = vk.queueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;

        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};

        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = (u32)devExts.length();
        dci.ppEnabledExtensionNames = devExts.data();
        VK_CHECK(vkCreateDevice(vk.phys, &dci, nullptr, &vk.device));
        vkGetDeviceQueue(vk.device, vk.queueFamily, 0, &vk.queue);

        if (vk.hasDmabuf) {
            vk.getMemoryFdProps = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(vk.device, "vkGetMemoryFdPropertiesKHR");

            if (!vk.getMemoryFdProps) {
                vk.hasDmabuf = false;
            }
        }
    }

    void destroyVulkan(DeviceVk& vk) noexcept {
        if (vk.device) {
            vkDestroyDevice(vk.device, nullptr);
        }

        if (vk.instance) {
            vkDestroyInstance(vk.instance, nullptr);
        }

        vk.device = VK_NULL_HANDLE;
        vk.instance = VK_NULL_HANDLE;
    }

    void queryDmabufFormats(const DeviceVk& vk, Vector<DmabufFormat>& out) {
        VkDrmFormatModifierPropertiesListEXT modList{VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
        VkFormatProperties2 props{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

        props.pNext = &modList;
        vkGetPhysicalDeviceFormatProperties2(vk.phys, kVkFormat, &props);

        Vector<VkDrmFormatModifierPropertiesEXT> mods;

        mods.zero(modList.drmFormatModifierCount);
        modList.pDrmFormatModifierProperties = mods.mutData();
        vkGetPhysicalDeviceFormatProperties2(vk.phys, kVkFormat, &props);

        for (const auto& m : mods) {
            if (m.drmFormatModifierPlaneCount != 1) {
                continue;
            }

            if (!(m.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
                continue;
            }

            out.pushBack({kFourccArgb, m.drmFormatModifier});
            out.pushBack({kFourccXrgb, m.drmFormatModifier});
        }

        sysO << "imway: dmabuf formats: "_sv << out.length() << " (modifiers per fourcc: "_sv << out.length() / 2 << ")"_sv << endL;
    }

    struct ScanBuf {
        ScanoutBuffer pub;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        u32 gemHandle = 0;
        u32 fbId = 0;
    };

    struct DumbBuffer {
        u32 handle = 0;
        u32 fbId = 0;
        u32 pitch = 0;
        u64 size = 0;
        u8* map = nullptr;
    };

    u32 getPropId(int fd, u32 objId, u32 objType, const char* name) {
        drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objId, objType);

        if (!props) {
            return 0;
        }

        u32 id = 0;

        for (u32 i = 0; i < props->count_props && !id; i++) {
            drmModePropertyRes* p = drmModeGetProperty(fd, props->props[i]);

            if (p) {
                if (!strcmp(p->name, name)) {
                    id = p->prop_id;
                }

                drmModeFreeProperty(p);
            }
        }

        drmModeFreeObjectProperties(props);

        return id;
    }

    u32 planeModifiers(int fd, u32 planeId, u32 fourcc, u64* out, u32 max) {
        u32 propId = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "IN_FORMATS");
        u32 n = 0;

        if (!propId) {
            out[n++] = DRM_FORMAT_MOD_LINEAR;

            return n;
        }

        drmModeObjectProperties* props = drmModeObjectGetProperties(fd, planeId, DRM_MODE_OBJECT_PLANE);

        if (!props) {
            return 0;
        }

        u64 blobId = 0;

        for (u32 i = 0; i < props->count_props; i++) {
            if (props->props[i] == propId) {
                blobId = props->prop_values[i];
            }
        }

        drmModeFreeObjectProperties(props);

        drmModePropertyBlobRes* blob = blobId ? drmModeGetPropertyBlob(fd, (u32)blobId) : nullptr;

        if (!blob) {
            out[n++] = DRM_FORMAT_MOD_LINEAR;

            return n;
        }

        const auto* hdr = (const drm_format_modifier_blob*)blob->data;
        const u32* formats = (const u32*)((const u8*)hdr + hdr->formats_offset);
        const auto* mods = (const drm_format_modifier*)((const u8*)hdr + hdr->modifiers_offset);
        u32 fmtIdx = UINT32_MAX;

        for (u32 i = 0; i < hdr->count_formats; i++) {
            if (formats[i] == fourcc) {
                fmtIdx = i;
            }
        }

        for (u32 i = 0; fmtIdx != UINT32_MAX && i < hdr->count_modifiers && n < max; i++) {
            const drm_format_modifier& m = mods[i];

            if (fmtIdx >= m.offset && fmtIdx < m.offset + 64 && (m.formats & (1ull << (fmtIdx - m.offset)))) {
                out[n++] = m.modifier;
            }
        }

        drmModeFreePropertyBlob(blob);

        return n;
    }

    void destroyScanBuf(const DeviceVk& vk, int fd, ScanBuf& sb) {
        if (sb.fbId) {
            drmModeRmFB(fd, sb.fbId);
        }

        if (sb.gemHandle) {
            drm_gem_close gc{};

            gc.handle = sb.gemHandle;
            drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &gc);
        }

        if (sb.pub.image) {
            vkDestroyImage(vk.device, sb.pub.image, nullptr);
        }

        if (sb.memory) {
            vkFreeMemory(vk.device, sb.memory, nullptr);
        }

        sb = ScanBuf{};
    }

    bool createScanBuf(const DeviceVk& vk, int fd, int w, int h, const u64* planeMods, u32 nPlaneMods, ScanBuf& sb) {
        VkDrmFormatModifierPropertiesListEXT modList{VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
        VkFormatProperties2 fprops{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

        fprops.pNext = &modList;
        vkGetPhysicalDeviceFormatProperties2(vk.phys, kVkFormat, &fprops);

        Vector<VkDrmFormatModifierPropertiesEXT> vkMods;

        vkMods.zero(modList.drmFormatModifierCount);
        modList.pDrmFormatModifierProperties = vkMods.mutData();
        vkGetPhysicalDeviceFormatProperties2(vk.phys, kVkFormat, &fprops);

        Vector<u64> cands;

        for (const auto& m : vkMods) {
            if (m.drmFormatModifierPlaneCount != 1) {
                continue;
            }

            if (!(m.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
                continue;
            }

            bool planeOk = false;

            for (u32 i = 0; i < nPlaneMods; i++) {
                if (planeMods[i] == m.drmFormatModifier) {
                    planeOk = true;
                }
            }

            if (!planeOk) {
                continue;
            }

            VkPhysicalDeviceImageDrmFormatModifierInfoEXT modInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT};

            modInfo.drmFormatModifier = m.drmFormatModifier;
            modInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VkPhysicalDeviceExternalImageFormatInfo extInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};

            extInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            extInfo.pNext = &modInfo;

            VkPhysicalDeviceImageFormatInfo2 ifi{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};

            ifi.pNext = &extInfo;
            ifi.format = kVkFormat;
            ifi.type = VK_IMAGE_TYPE_2D;
            ifi.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
            ifi.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

            VkExternalImageFormatProperties extProps{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
            VkImageFormatProperties2 iprops{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};

            iprops.pNext = &extProps;

            if (vkGetPhysicalDeviceImageFormatProperties2(vk.phys, &ifi, &iprops) != VK_SUCCESS) {
                continue;
            }

            if (!(extProps.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT)) {
                continue;
            }

            cands.pushBack(m.drmFormatModifier);
        }

        if (cands.empty()) {
            sysE << "imway: scanout: no common modifier (vulkan x plane)"_sv << endL;

            return false;
        }

        VkImageDrmFormatModifierListCreateInfoEXT modCreate{VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT};

        modCreate.drmFormatModifierCount = (u32)cands.length();
        modCreate.pDrmFormatModifiers = cands.data();

        VkExternalMemoryImageCreateInfo extCreate{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};

        extCreate.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        extCreate.pNext = &modCreate;

        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};

        ici.pNext = &extCreate;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = kVkFormat;
        ici.extent = {(u32)w, (u32)h, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(vk.device, &ici, nullptr, &sb.pub.image) != VK_SUCCESS) {
            sysE << "imway: scanout: vkCreateImage failed"_sv << endL;

            return false;
        }

        VkMemoryRequirements req{};

        vkGetImageMemoryRequirements(vk.device, sb.pub.image, &req);

        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};

        dedicated.image = sb.pub.image;

        VkExportMemoryAllocateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};

        exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        exportInfo.pNext = &dedicated;

        u32 typeIdx = 0;

        while (typeIdx < 32 && !(req.memoryTypeBits & (1u << typeIdx))) {
            typeIdx++;
        }

        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

        mai.pNext = &exportInfo;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = typeIdx;

        if (vkAllocateMemory(vk.device, &mai, nullptr, &sb.memory) != VK_SUCCESS || vkBindImageMemory(vk.device, sb.pub.image, sb.memory, 0) != VK_SUCCESS) {
            sysE << "imway: scanout: exportable allocation failed"_sv << endL;
            destroyScanBuf(vk, fd, sb);

            return false;
        }

        auto getModProps = (PFN_vkGetImageDrmFormatModifierPropertiesEXT)vkGetDeviceProcAddr(vk.device, "vkGetImageDrmFormatModifierPropertiesEXT");
        auto getMemoryFd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(vk.device, "vkGetMemoryFdKHR");

        VkImageDrmFormatModifierPropertiesEXT chosen{VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT};

        if (!getModProps || !getMemoryFd || getModProps(vk.device, sb.pub.image, &chosen) != VK_SUCCESS) {
            destroyScanBuf(vk, fd, sb);

            return false;
        }

        VkImageSubresource sub{VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT, 0, 0};
        VkSubresourceLayout layout{};

        vkGetImageSubresourceLayout(vk.device, sb.pub.image, &sub, &layout);

        VkMemoryGetFdInfoKHR gfi{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};

        gfi.memory = sb.memory;
        gfi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

        int dmaFd = -1;

        if (getMemoryFd(vk.device, &gfi, &dmaFd) != VK_SUCCESS || dmaFd < 0) {
            sysE << "imway: scanout: dmabuf export failed (udmabuf?)"_sv << endL;
            destroyScanBuf(vk, fd, sb);

            return false;
        }

        if (drmPrimeFDToHandle(fd, dmaFd, &sb.gemHandle) != 0) {
            sysE << "imway: scanout: kms prime import failed, errno "_sv << errno << endL;
            close(dmaFd);
            destroyScanBuf(vk, fd, sb);

            return false;
        }

        close(dmaFd);

        u32 handles[4] = {sb.gemHandle};
        u32 pitches[4] = {(u32)layout.rowPitch};
        u32 offsets[4] = {(u32)layout.offset};
        u64 modifiers[4] = {chosen.drmFormatModifier};

        if (drmModeAddFB2WithModifiers(fd, w, h, DRM_FORMAT_XRGB8888, handles, pitches, offsets, modifiers, &sb.fbId, DRM_MODE_FB_MODIFIERS) != 0) {
            sysE << "imway: scanout: AddFB2WithModifiers failed, errno "_sv << errno << endL;
            destroyScanBuf(vk, fd, sb);

            return false;
        }

        return true;
    }

    struct KmsOutput: public ::Output, public SessionListener {
        int fd = -1;
        int ttyFd = -1;
        long oldKbMode = -1;
        bool sessionActive = true;
        DeviceVk vk{};

        ScanBuf scan[2];
        int scanCount = 0;
        int scanNext = 0;

        u32 connectorId = 0;
        u32 crtcId = 0;
        u32 planeId = 0;
        drmModeModeInfo mode{};
        u32 modeBlob = 0;

        u32 connCrtcId = 0;
        u32 crtcModeId = 0, crtcActive = 0;
        u32 plFbId = 0, plCrtcId = 0;
        u32 plSrcX = 0, plSrcY = 0, plSrcW = 0, plSrcH = 0;
        u32 plCrtcX = 0, plCrtcY = 0, plCrtcW = 0, plCrtcH = 0;

        DumbBuffer bufs[2];
        int nextBuf = 0;
        bool flipPending = false;
        bool started = false;
        bool modesetDone = false;

        KmsOutput(int drmFd, const DeviceVk& v, Session& session, const char* connector, const char* modeStr);

        void sessionEnabled() override;
        void sessionDisabled() override;
        ~KmsOutput() noexcept;

        int width() const override {
            return mode.hdisplay;
        }

        int height() const override {
            return mode.vdisplay;
        }

        double refresh() const override {
            return mode.vrefresh > 0 ? mode.vrefresh : 60.0;
        }

        void pickPipe(const char* connector, const char* modeStr);
        void createDumb(DumbBuffer& b);
        bool commit(u32 fbId, bool doModeset);
        void setupVt();
        void restoreVt() noexcept;

        bool start() override;

        bool ready() const override {
            return started && !flipPending && sessionActive;
        }

        bool vsynced() const override {
            return true;
        }

        int scanoutCount() const override {
            return scanCount;
        }

        ScanoutBuffer* scanoutBuffer(int i) override {
            return &scan[i].pub;
        }

        int acquire() override {
            return scanNext;
        }

        void presentImage(int i) override;

        bool presentNeedsPixels() const override {
            return scanCount == 0;
        }

        void present(const void* pixels) override;
    };

    void pageFlipHandler(int, unsigned, unsigned, unsigned, void* data) {
        ((KmsOutput*)data)->flipPending = false;
    }

    void drmIoCb(struct ev_loop*, ev_io* w, int) {
        drmEventContext ctx{};

        ctx.version = 2;
        ctx.page_flip_handler = pageFlipHandler;
        drmHandleEvent((int)(intptr_t)w->data, &ctx);
    }

    struct KmsDevice: public Device {
        ObjPool* pool = nullptr;
        struct ev_loop* loop = nullptr;
        Session* session = nullptr;
        int fd = -1;
        char path[64] = {};
        DeviceVk vk{};
        Vector<DmabufFormat> formats;
        ev_io drmIo{};

        KmsDevice(ObjPool* p, struct ev_loop* evLoop, Session& s, const char* devPath);
        ~KmsDevice() noexcept;

        size_t dmabufFormatCount() const override {
            return formats.length();
        }

        DmabufFormat dmabufFormat(size_t i) const override {
            return formats[i];
        }

        ::Output* createOutput(const char* connector, const char* modeStr) override {
            return pool->make<KmsOutput>(fd, vk, *session, connector, modeStr);
        }

        Renderer* createRenderer(Scene& scene, ::Output& output, FrameListener& listener, const char* fontPath, int framesLimit) override {
            return Renderer::create(pool, loop, scene, output, vk, listener, fontPath, framesLimit);
        }
    };

    struct HeadlessOutput: public ::Output {
        int w = 0, h = 0;
        double hz = 60.0;

        HeadlessOutput(int width, int height, double refresh) : w(width), h(height), hz(refresh) {
        }

        int width() const override {
            return w;
        }

        int height() const override {
            return h;
        }

        double refresh() const override {
            return hz;
        }

        bool start() override {
            return true;
        }

        bool ready() const override {
            return true;
        }

        bool vsynced() const override {
            return false;
        }

        int scanoutCount() const override {
            return 0;
        }

        ScanoutBuffer* scanoutBuffer(int) override {
            return nullptr;
        }

        int acquire() override {
            return -1;
        }

        void presentImage(int) override {
        }

        bool presentNeedsPixels() const override {
            return false;
        }

        void present(const void*) override {
        }
    };

    struct HeadlessDevice: public Device {
        ObjPool* pool = nullptr;
        struct ev_loop* loop = nullptr;
        DeviceVk vk{};
        Vector<DmabufFormat> formats;

        HeadlessDevice(ObjPool* p, struct ev_loop* evLoop);
        ~HeadlessDevice() noexcept;

        size_t dmabufFormatCount() const override {
            return formats.length();
        }

        DmabufFormat dmabufFormat(size_t i) const override {
            return formats[i];
        }

        ::Output* createOutput(const char*, const char* modeStr) override;

        Renderer* createRenderer(Scene& scene, ::Output& output, FrameListener& listener, const char* fontPath, int framesLimit) override {
            return Renderer::create(pool, loop, scene, output, vk, listener, fontPath, framesLimit);
        }
    };

    int openKmsNode(Session& session, const char* devPath, char* outPath, size_t outLen) {
        if (devPath) {
            int fd = session.openDevice(devPath);

            if (fd < 0) {
                Errno(-fd).raise(StringBuilder() << "kms: open "_sv << devPath);
            }

            snprintf(outPath, outLen, "%s", devPath);

            return fd;
        }

        for (int i = 0; i < 8; i++) {
            char p[32];

            snprintf(p, sizeof(p), "/dev/dri/card%d", i);

            int fd = session.openDevice(p);

            if (fd < 0) {
                continue;
            }

            if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
                session.closeDevice(fd);

                continue;
            }

            snprintf(outPath, outLen, "%s", p);

            return fd;
        }

        Errno().raise(StringBuilder() << "kms: no device with atomic support under /dev/dri"_sv);

        return -1;
    }
}

KmsDevice::KmsDevice(ObjPool* p, struct ev_loop* evLoop, Session& s, const char* devPath) : pool(p), loop(evLoop), session(&s) {
    fd = openKmsNode(s, devPath, path, sizeof(path));

    STD_VERIFY(drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) == 0);
    STD_VERIFY(drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0);

    initVulkan(vk, fd);

    if (vk.hasDmabuf) {
        queryDmabufFormats(vk, formats);
    }

    ev_io_init(&drmIo, drmIoCb, fd, EV_READ);
    drmIo.data = (void*)(intptr_t)fd;
    ev_io_start(loop, &drmIo);

    sysO << "imway: device "_sv << (const char*)path << endL;
}

KmsDevice::~KmsDevice() noexcept {
    ev_io_stop(loop, &drmIo);
    destroyVulkan(vk);

    if (fd >= 0) {
        session->closeDevice(fd);
        fd = -1;
    }
}

KmsOutput::KmsOutput(int drmFd, const DeviceVk& v, Session& session, const char* connector, const char* modeStr) : fd(drmFd), vk(v) {
    session.addListener(this);
    pickPipe(connector, modeStr);

    connCrtcId = getPropId(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    crtcModeId = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    crtcActive = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    plFbId = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "FB_ID");
    plCrtcId = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    plSrcX = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_X");
    plSrcY = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    plSrcW = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_W");
    plSrcH = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_H");
    plCrtcX = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    plCrtcY = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    plCrtcW = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    plCrtcH = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_H");

    drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &modeBlob);

    if (vk.hasDmabuf) {
        u64 mods[64];
        u32 nmods = planeModifiers(fd, planeId, DRM_FORMAT_XRGB8888, mods, 64);

        for (auto& sb : scan) {
            if (createScanBuf(vk, fd, mode.hdisplay, mode.vdisplay, mods, nmods, sb)) {
                scanCount++;
            } else {
                break;
            }
        }

        if (scanCount < 2) {
            for (auto& sb : scan) {
                destroyScanBuf(vk, fd, sb);
            }

            scanCount = 0;
        }
    }

    if (scanCount > 0) {
        sysO << "imway: scanout swapchain: "_sv << scanCount << " images"_sv << endL;
    } else {
        sysO << "imway: dumb-buffer path (no zero-copy scanout)"_sv << endL;

        for (auto& b : bufs) {
            createDumb(b);
        }
    }

    sysO << "imway: kms output: "_sv << mode.hdisplay << "x"_sv << mode.vdisplay << "@"_sv << mode.vrefresh << ", connector "_sv << connectorId << ", crtc "_sv << crtcId << ", plane "_sv << planeId << endL;
}

KmsOutput::~KmsOutput() noexcept {
    restoreVt();

    for (auto& sb : scan) {
        destroyScanBuf(vk, fd, sb);
    }

    for (auto& b : bufs) {
        if (b.map && b.map != MAP_FAILED) {
            munmap(b.map, b.size);
        }

        if (b.fbId) {
            drmModeRmFB(fd, b.fbId);
        }

        if (b.handle) {
            drm_mode_destroy_dumb d{};

            d.handle = b.handle;
            drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
        }
    }

    if (modeBlob) {
        drmModeDestroyPropertyBlob(fd, modeBlob);
    }
}

void KmsOutput::pickPipe(const char* connector, const char* modeStr) {
    drmModeRes* res = drmModeGetResources(fd);

    STD_VERIFY(res);

    drmModeConnector* conn = nullptr;

    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector* c = drmModeGetConnector(fd, res->connectors[i]);

        if (!c) {
            continue;
        }

        bool ok = c->connection == DRM_MODE_CONNECTED && c->count_modes > 0;

        if (ok && connector) {
            char name[64];

            connectorName(c, name, sizeof(name));
            ok = !strcmp(name, connector);
        }

        if (ok) {
            conn = c;
        } else {
            drmModeFreeConnector(c);
        }
    }

    if (!conn && connector) {
        sysE << "imway: connector "_sv << connector << " not found or not connected (see imway --list)"_sv << endL;
    }

    STD_VERIFY(conn);

    connectorId = conn->connector_id;

    if (modeStr) {
        ModeSpec want{};

        STD_VERIFY(parseModeSpec(modeStr, want));

        bool found = false;

        for (int i = 0; i < conn->count_modes && !found; i++) {
            const drmModeModeInfo& m = conn->modes[i];

            if (m.hdisplay == want.w && m.vdisplay == want.h && (want.hz == 0 || m.vrefresh == (u32)want.hz)) {
                mode = m;
                found = true;
            }
        }

        if (!found) {
            sysE << "imway: mode "_sv << modeStr << " not offered by the connector (see imway --list)"_sv << endL;
        }

        STD_VERIFY(found);
    } else {
        mode = conn->modes[0];

        for (int i = 0; i < conn->count_modes; i++) {
            if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
                mode = conn->modes[i];

                break;
            }
        }
    }

    drmModeEncoder* enc = conn->encoder_id ? drmModeGetEncoder(fd, conn->encoder_id) : nullptr;

    if (!enc && conn->count_encoders > 0) {
        enc = drmModeGetEncoder(fd, conn->encoders[0]);
    }

    STD_VERIFY(enc);

    if (enc->crtc_id) {
        crtcId = enc->crtc_id;
    } else {
        for (int i = 0; i < res->count_crtcs; i++) {
            if (enc->possible_crtcs & (1 << i)) {
                crtcId = res->crtcs[i];

                break;
            }
        }
    }

    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    STD_VERIFY(crtcId);

    int crtcIndex = -1;

    for (int i = 0; i < res->count_crtcs; i++) {
        if (res->crtcs[i] == crtcId) {
            crtcIndex = i;
        }
    }

    drmModeFreeResources(res);

    drmModePlaneRes* planes = drmModeGetPlaneResources(fd);

    for (u32 i = 0; i < planes->count_planes && !planeId; i++) {
        drmModePlane* p = drmModeGetPlane(fd, planes->planes[i]);

        if (!p) {
            continue;
        }

        if (p->possible_crtcs & (1u << crtcIndex)) {
            u32 typeProp = getPropId(fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type");
            drmModeObjectProperties* pp = drmModeObjectGetProperties(fd, p->plane_id, DRM_MODE_OBJECT_PLANE);

            for (u32 j = 0; j < pp->count_props; j++) {
                if (pp->props[j] == typeProp && pp->prop_values[j] == DRM_PLANE_TYPE_PRIMARY) {
                    planeId = p->plane_id;
                }
            }

            drmModeFreeObjectProperties(pp);
        }

        drmModeFreePlane(p);
    }

    drmModeFreePlaneResources(planes);
    STD_VERIFY(planeId);
}

void KmsOutput::createDumb(DumbBuffer& b) {
    drm_mode_create_dumb create{};

    create.width = mode.hdisplay;
    create.height = mode.vdisplay;
    create.bpp = 32;
    STD_VERIFY(drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) == 0);

    b.handle = create.handle;
    b.pitch = create.pitch;
    b.size = create.size;

    u32 handles[4] = {b.handle}, pitches[4] = {b.pitch}, offsets[4] = {};

    STD_VERIFY(drmModeAddFB2(fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &b.fbId, 0) == 0);

    drm_mode_map_dumb mapReq{};

    mapReq.handle = b.handle;
    STD_VERIFY(drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mapReq) == 0);

    b.map = (u8*)mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)mapReq.offset);
    STD_VERIFY(b.map != MAP_FAILED);
}

bool KmsOutput::commit(u32 fbId, bool doModeset) {
    drmModeAtomicReq* req = drmModeAtomicAlloc();

    if (doModeset) {
        drmModeAtomicAddProperty(req, connectorId, connCrtcId, crtcId);
        drmModeAtomicAddProperty(req, crtcId, crtcModeId, modeBlob);
        drmModeAtomicAddProperty(req, crtcId, crtcActive, 1);
    }

    drmModeAtomicAddProperty(req, planeId, plFbId, fbId);
    drmModeAtomicAddProperty(req, planeId, plCrtcId, crtcId);
    drmModeAtomicAddProperty(req, planeId, plSrcX, 0);
    drmModeAtomicAddProperty(req, planeId, plSrcY, 0);
    drmModeAtomicAddProperty(req, planeId, plSrcW, (u64)mode.hdisplay << 16);
    drmModeAtomicAddProperty(req, planeId, plSrcH, (u64)mode.vdisplay << 16);
    drmModeAtomicAddProperty(req, planeId, plCrtcX, 0);
    drmModeAtomicAddProperty(req, planeId, plCrtcY, 0);
    drmModeAtomicAddProperty(req, planeId, plCrtcW, mode.hdisplay);
    drmModeAtomicAddProperty(req, planeId, plCrtcH, mode.vdisplay);

    u32 flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;

    if (doModeset) {
        flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
    }

    int ret = drmModeAtomicCommit(fd, req, flags, this);

    drmModeAtomicFree(req);

    if (ret != 0) {
        if (errno != EBUSY) {
            sysE << "imway: kms atomic commit failed, errno "_sv << errno << endL;
        }

        return false;
    }

    flipPending = true;

    return true;
}

void KmsOutput::setupVt() {
    ttyFd = open("/dev/tty1", O_RDWR | O_CLOEXEC);

    if (ttyFd < 0) {
        sysE << "imway: /dev/tty1 unavailable, input will leak to console"_sv << endL;

        return;
    }

    ioctl(ttyFd, KDGKBMODE, &oldKbMode);
    ioctl(ttyFd, KDSKBMODE, K_OFF);
    ioctl(ttyFd, KDSETMODE, KD_GRAPHICS);
}

void KmsOutput::restoreVt() noexcept {
    if (ttyFd < 0) {
        return;
    }

    ioctl(ttyFd, KDSETMODE, KD_TEXT);

    if (oldKbMode != -1) {
        ioctl(ttyFd, KDSKBMODE, oldKbMode);
    }

    close(ttyFd);
    ttyFd = -1;
}

bool KmsOutput::start() {
    setupVt();

    if (scanCount > 0) {
        started = true;

        return true;
    }

    memset(bufs[0].map, 0, bufs[0].size);

    if (!commit(bufs[0].fbId, true)) {
        sysE << "imway: kms modeset failed"_sv << endL;

        return false;
    }

    nextBuf = 1;
    started = true;
    modesetDone = true;

    return true;
}

void KmsOutput::presentImage(int i) {
    if (!started || flipPending || !sessionActive) {
        return;
    }

    if (commit(scan[i].fbId, !modesetDone)) {
        modesetDone = true;
        scanNext = i ^ 1;
    }
}

void KmsOutput::sessionDisabled() {
    sessionActive = false;
    sysO << "imway: session disabled (vt switch away)"_sv << endL;
}

void KmsOutput::sessionEnabled() {
    bool comeback = !sessionActive;

    sessionActive = true;
    flipPending = false;

    if (!comeback || !modesetDone) {
        return;
    }

    u32 lastFb = scanCount > 0 ? scan[scanNext ^ 1].fbId : bufs[nextBuf ^ 1].fbId;

    if (commit(lastFb, true)) {
        sysO << "imway: session enabled, remodeset"_sv << endL;
    }
}

void KmsOutput::present(const void* pixels) {
    if (!modesetDone || flipPending || !sessionActive || !pixels) {
        return;
    }

    DumbBuffer& b = bufs[nextBuf];
    const auto* src = (const u8*)pixels;
    size_t row = (size_t)mode.hdisplay * 4;

    if (b.pitch == row) {
        memcpy(b.map, src, row * mode.vdisplay);
    } else {
        for (int y = 0; y < mode.vdisplay; y++) {
            memcpy(b.map + (size_t)y * b.pitch, src + (size_t)y * row, row);
        }
    }

    if (commit(b.fbId, false)) {
        nextBuf ^= 1;
    }
}

HeadlessDevice::HeadlessDevice(ObjPool* p, struct ev_loop* evLoop) : pool(p), loop(evLoop) {
    initVulkan(vk, -1);

    if (vk.hasDmabuf) {
        queryDmabufFormats(vk, formats);
    }
}

HeadlessDevice::~HeadlessDevice() noexcept {
    destroyVulkan(vk);
}

::Output* HeadlessDevice::createOutput(const char*, const char* modeStr) {
    ModeSpec m{1280, 800, 60};

    if (modeStr) {
        STD_VERIFY(parseModeSpec(modeStr, m));
    }

    return pool->make<HeadlessOutput>(m.w, m.h, m.hz > 0 ? m.hz : 60.0);
}

Device* Device::createKms(ObjPool* pool, struct ev_loop* loop, Session& session, const char* devPath) {
    return pool->make<KmsDevice>(pool, loop, session, devPath);
}

Device* Device::createHeadless(ObjPool* pool, struct ev_loop* loop) {
    return pool->make<HeadlessDevice>(pool, loop);
}

void Device::list() {
    for (int i = 0; i < 8; i++) {
        char p[32];

        snprintf(p, sizeof(p), "/dev/dri/card%d", i);

        int fd = open(p, O_RDWR | O_CLOEXEC | O_NONBLOCK);

        if (fd < 0) {
            continue;
        }

        bool atomic = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0;
        drmVersion* ver = drmGetVersion(fd);

        sysO << (const char*)p << ": "_sv << (ver && ver->name ? ver->name : "?") << (atomic ? ", atomic"_sv : ", NO atomic (unusable)"_sv) << endL;

        if (ver) {
            drmFreeVersion(ver);
        }

        drmModeRes* res = drmModeGetResources(fd);

        if (res) {
            for (int c = 0; c < res->count_connectors; c++) {
                drmModeConnector* conn = drmModeGetConnector(fd, res->connectors[c]);

                if (!conn) {
                    continue;
                }

                char name[64];

                connectorName(conn, name, sizeof(name));
                sysO << "  "_sv << (const char*)name << (conn->connection == DRM_MODE_CONNECTED ? ": connected"_sv : ": disconnected"_sv);

                for (int m = 0; m < conn->count_modes; m++) {
                    const drmModeModeInfo& mi = conn->modes[m];

                    sysO << " "_sv << mi.hdisplay << "x"_sv << mi.vdisplay << "@"_sv << mi.vrefresh << ((mi.type & DRM_MODE_TYPE_PREFERRED) ? "*"_sv : ""_sv);
                }

                sysO << endL;
                drmModeFreeConnector(conn);
            }

            drmModeFreeResources(res);
        }

        close(fd);
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};

    app.pApplicationName = "imway";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};

    instInfo.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;

    if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS) {
        sysO << "vulkan: unavailable"_sv << endL;

        return;
    }

    u32 n = 0;

    vkEnumeratePhysicalDevices(instance, &n, nullptr);

    Vector<VkPhysicalDevice> devs;

    devs.zero(n);
    vkEnumeratePhysicalDevices(instance, &n, devs.mutData());

    for (VkPhysicalDevice d : devs) {
        VkPhysicalDeviceDrmPropertiesEXT drm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};

        p2.pNext = &drm;
        vkGetPhysicalDeviceProperties2(d, &p2);
        sysO << "vulkan: "_sv << (const char*)p2.properties.deviceName;

        if (drm.hasPrimary) {
            sysO << " (drm "_sv << (int)drm.primaryMajor << ":"_sv << (int)drm.primaryMinor << ")"_sv;
        } else {
            sysO << " (no drm node)"_sv;
        }

        sysO << endL;
    }

    vkDestroyInstance(instance, nullptr);
}
