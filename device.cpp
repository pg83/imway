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
#include <math.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux/kd.h>
#include <libudev.h>
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

    void connectorName(const drmModeConnector* c, CStr<64>& out) {
        const char* t = drmModeGetConnectorTypeName(c->connector_type);

        out << (t ? t : "Unknown") << "-"_sv << c->connector_type_id;
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
                if (StringView(e.extensionName) == StringView(name)) {
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

        if (hasExt(vk.phys, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) {
            VkPhysicalDeviceDrmPropertiesEXT drm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
            VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};

            p2.pNext = &drm;
            vkGetPhysicalDeviceProperties2(vk.phys, &p2);

            if (drm.hasRender) {
                vk.renderDev = makedev((u32)drm.renderMajor, (u32)drm.renderMinor);
            } else if (drm.hasPrimary) {
                vk.renderDev = makedev((u32)drm.primaryMajor, (u32)drm.primaryMinor);
            }
        }

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

            if (hasExt(vk.phys, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME)) {
                VkPhysicalDeviceExternalSemaphoreInfo semInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};

                semInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

                VkExternalSemaphoreProperties semProps{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};

                vkGetPhysicalDeviceExternalSemaphoreProperties(vk.phys, &semInfo, &semProps);

                if ((semProps.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) && (semProps.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)) {
                    devExts.pushBack(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
                    vk.hasSyncFd = true;
                }
            }

            if (!vk.hasSyncFd) {
                sysO << "imway: no SYNC_FD semaphores, implicit-sync bridge disabled"_sv << endL;
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
            if (m.drmFormatModifierPlaneCount > (u32)kDmabufMaxPlanes) {
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
                if (StringView(p->name) == StringView(name)) {
                    id = p->prop_id;
                }

                drmModeFreeProperty(p);
            }
        }

        drmModeFreeObjectProperties(props);

        return id;
    }

    u64 getPropValue(int fd, u32 objId, u32 objType, const char* name, u64 def) {
        drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objId, objType);

        if (!props) {
            return def;
        }

        u64 value = def;

        for (u32 i = 0; i < props->count_props; i++) {
            drmModePropertyRes* p = drmModeGetProperty(fd, props->props[i]);

            if (p) {
                if (StringView(p->name) == StringView(name)) {
                    value = props->prop_values[i];
                }

                drmModeFreeProperty(p);
            }
        }

        drmModeFreeObjectProperties(props);

        return value;
    }

    bool getEnumProp(int fd, u32 objId, u32 objType, const char* name, const char* valueName, u32* propId, u64* value) {
        drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objId, objType);

        if (!props) {
            return false;
        }

        bool found = false;

        for (u32 i = 0; i < props->count_props && !found; i++) {
            drmModePropertyRes* p = drmModeGetProperty(fd, props->props[i]);

            if (!p) {
                continue;
            }

            if (StringView(p->name) == StringView(name)) {
                for (int e = 0; e < p->count_enums; e++) {
                    if (StringView(p->enums[e].name) == StringView(valueName)) {
                        *propId = p->prop_id;
                        *value = p->enums[e].value;
                        found = true;
                    }
                }
            }

            drmModeFreeProperty(p);
        }

        drmModeFreeObjectProperties(props);

        return found;
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

    bool createScanBuf(const DeviceVk& vk, int fd, int w, int h, const u64* planeMods, u32 nPlaneMods, VkFormat vkFmt, u32 fourcc, ScanBuf& sb) {
        VkDrmFormatModifierPropertiesListEXT modList{VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
        VkFormatProperties2 fprops{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

        fprops.pNext = &modList;
        vkGetPhysicalDeviceFormatProperties2(vk.phys, vkFmt, &fprops);

        Vector<VkDrmFormatModifierPropertiesEXT> vkMods;

        vkMods.zero(modList.drmFormatModifierCount);
        modList.pDrmFormatModifierProperties = vkMods.mutData();
        vkGetPhysicalDeviceFormatProperties2(vk.phys, vkFmt, &fprops);

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
            ifi.format = vkFmt;
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
        ici.format = vkFmt;
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

        if (drmModeAddFB2WithModifiers(fd, w, h, fourcc, handles, pitches, offsets, modifiers, &sb.fbId, DRM_MODE_FB_MODIFIERS) != 0) {
            sysE << "imway: scanout: AddFB2WithModifiers failed, errno "_sv << errno << endL;
            destroyScanBuf(vk, fd, sb);

            return false;
        }

        sb.pub.format = vkFmt;

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

        double hdrNits = 0;
        bool hdrActive = false;
        u32 connColorspace = 0, connHdrMeta = 0;
        u64 colorspaceBt2020 = 0;
        u32 crtcDegammaProp = 0, crtcCtmProp = 0, crtcGammaProp = 0;
        u32 hdrMetaBlob = 0, degammaBlob = 0, ctmBlob = 0, gammaBlob = 0;
        u64 gamLutSize = 0;
        bool gammaDirty = false;
        double tempK = 0; // night light color temperature, 0 = neutral

        // last completed pageflip, for presentation-time feedback
        u64 flipNs = 0;
        u32 flipSeq = 0;

        u32 cursorPlaneId = 0;
        u32 cuFbId = 0, cuCrtcId = 0;
        u32 cuSrcX = 0, cuSrcY = 0, cuSrcW = 0, cuSrcH = 0;
        u32 cuCrtcX = 0, cuCrtcY = 0, cuCrtcW = 0, cuCrtcH = 0;
        DumbBuffer cursorBuf{};
        int curW = 0, curH = 0;
        bool cursorEnabled = false;
        bool cursorVisible = false;
        int cursorX = 0, cursorY = 0;

        DumbBuffer bufs[2];
        int nextBuf = 0;
        bool flipPending = false;
        bool started = false;
        bool modesetDone = false;
        bool connectorConnected = true;
        bool powered = true;

        KmsOutput(int drmFd, const DeviceVk& v, Session& session, const char* connector, const char* modeStr, double hdrWhiteNits);

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
        bool setupHdr();
        void buildGammaLut();

        double sdrWhiteNits() const override {
            return hdrActive ? hdrNits : 0;
        }

        void setSdrWhite(double nits) override {
            if (!hdrActive || nits <= 0 || nits == hdrNits) {
                return;
            }

            hdrNits = nits;
            buildGammaLut();
            gammaDirty = true;
        }

        void setColorTemp(double kelvin) override {
            double k = kelvin <= 0 || kelvin >= 6500 ? 0 : kelvin;

            if (k == tempK) {
                return;
            }

            tempK = k;
            buildGammaLut();
            gammaDirty = true;
        }

        bool lastFlip(u64& nsec, u32& seq) const override {
            if (!flipNs) {
                return false;
            }

            nsec = flipNs;
            seq = flipSeq;

            return true;
        }
        void createDumb(DumbBuffer& b, u32 w, u32 h, u32 format);
        int tryCommit(u32 fbId, bool doModeset, bool withCursor);
        bool commit(u32 fbId, bool doModeset);
        void addCursorProps(drmModeAtomicReq* req);

        int cursorCapW() const override {
            return curW;
        }

        int cursorCapH() const override {
            return curH;
        }

        void setCursorImage(const u32* argb) override;
        void setCursorPos(int x, int y, bool visible) override;
        void setPowerSave(bool on) override;
        void setupVt();
        void restoreVt() noexcept;

        bool start() override;

        bool ready() const override {
            return started && !flipPending && sessionActive && connectorConnected && powered;
        }

        void hotplug();

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

    void pageFlipHandler(int, unsigned seq, unsigned sec, unsigned usec, void* data) {
        auto* out = (KmsOutput*)data;

        out->flipPending = false;
        out->flipNs = (u64)sec * 1000000000ull + (u64)usec * 1000ull;
        out->flipSeq = seq;
    }

    void drmIoCb(struct ev_loop*, ev_io* w, int) {
        drmEventContext ctx{};

        ctx.version = 2;
        ctx.page_flip_handler = pageFlipHandler;
        drmHandleEvent((int)(intptr_t)w->data, &ctx);
    }

    void udevIoCb(struct ev_loop*, ev_io* w, int);

    struct KmsDevice: public Device {
        ObjPool* pool = nullptr;
        struct ev_loop* loop = nullptr;
        Session* session = nullptr;
        int fd = -1;
        CStr<64> path;
        DeviceVk vk{};
        Vector<DmabufFormat> formats;
        ev_io drmIo{};
        udev* ud = nullptr;
        udev_monitor* mon = nullptr;
        ev_io udevIo{};
        KmsOutput* output = nullptr;

        KmsDevice(ObjPool* p, struct ev_loop* evLoop, Session& s, const char* devPath);
        ~KmsDevice() noexcept;

        int drmFd() const override {
            return fd;
        }

        unsigned long long renderDevice() const override {
            return vk.renderDev;
        }

        size_t dmabufFormatCount() const override {
            return formats.length();
        }

        DmabufFormat dmabufFormat(size_t i) const override {
            return formats[i];
        }

        ::Output* createOutput(const char* connector, const char* modeStr, double hdrNits) override {
            output = pool->make<KmsOutput>(fd, vk, *session, connector, modeStr, hdrNits);

            return output;
        }

        Renderer* createRenderer(Scene& scene, ::Output& output, FrameListener& listener, const char* fontPath, float uiScale, int framesLimit) override {
            return Renderer::create(pool, loop, scene, output, vk, listener, fontPath, uiScale, framesLimit);
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

        int cursorCapW() const override {
            return 0;
        }

        int cursorCapH() const override {
            return 0;
        }

        void setCursorImage(const u32*) override {
        }

        void setCursorPos(int, int, bool) override {
        }

        void setPowerSave(bool) override {
        }

        double sdrWhiteNits() const override {
            return 0;
        }

        void setSdrWhite(double) override {
        }

        void setColorTemp(double) override {
        }

        bool lastFlip(u64&, u32&) const override {
            return false;
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
        int syncFd = -1;

        HeadlessDevice(ObjPool* p, struct ev_loop* evLoop);
        ~HeadlessDevice() noexcept;

        int drmFd() const override {
            return syncFd;
        }

        unsigned long long renderDevice() const override {
            return vk.renderDev;
        }

        size_t dmabufFormatCount() const override {
            return formats.length();
        }

        DmabufFormat dmabufFormat(size_t i) const override {
            return formats[i];
        }

        ::Output* createOutput(const char*, const char* modeStr, double hdrNits) override;

        Renderer* createRenderer(Scene& scene, ::Output& output, FrameListener& listener, const char* fontPath, float uiScale, int framesLimit) override {
            return Renderer::create(pool, loop, scene, output, vk, listener, fontPath, uiScale, framesLimit);
        }
    };

    int openKmsNode(Session& session, const char* devPath, CStr<64>& outPath) {
        if (devPath) {
            int fd = session.openDevice(devPath);

            if (fd < 0) {
                Errno(-fd).raise(StringBuilder() << "kms: open "_sv << devPath);
            }

            outPath << devPath;

            return fd;
        }

        for (int i = 0; i < 8; i++) {
            CStr<64> p;

            p << "/dev/dri/card"_sv << i;

            int fd = session.openDevice(p.cStr());

            if (fd < 0) {
                continue;
            }

            if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
                session.closeDevice(fd);

                continue;
            }

            outPath << p.view();

            return fd;
        }

        Errno().raise(StringBuilder() << "kms: no device with atomic support under /dev/dri"_sv);

        return -1;
    }
}

KmsDevice::KmsDevice(ObjPool* p, struct ev_loop* evLoop, Session& s, const char* devPath) : pool(p), loop(evLoop), session(&s) {
    fd = openKmsNode(s, devPath, path);

    STD_VERIFY(drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) == 0);
    STD_VERIFY(drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0);

    initVulkan(vk, fd);
    vk.drmFd = fd;

    if (vk.hasDmabuf) {
        queryDmabufFormats(vk, formats);
    }

    ev_io_init(&drmIo, drmIoCb, fd, EV_READ);
    drmIo.data = (void*)(intptr_t)fd;
    ev_io_start(loop, &drmIo);

    ud = udev_new();

    if (ud) {
        // "kernel" = raw uevents, works with or without a running udevd
        mon = udev_monitor_new_from_netlink(ud, "kernel");
    }

    if (mon) {
        udev_monitor_filter_add_match_subsystem_devtype(mon, "drm", nullptr);
        udev_monitor_enable_receiving(mon);
        ev_io_init(&udevIo, udevIoCb, udev_monitor_get_fd(mon), EV_READ);
        udevIo.data = this;
        ev_io_start(loop, &udevIo);
    }

    sysO << "imway: device "_sv << path.view() << endL;
}

KmsDevice::~KmsDevice() noexcept {
    if (mon) {
        ev_io_stop(loop, &udevIo);
        udev_monitor_unref(mon);
    }

    if (ud) {
        udev_unref(ud);
    }

    ev_io_stop(loop, &drmIo);
    destroyVulkan(vk);

    if (fd >= 0) {
        session->closeDevice(fd);
        fd = -1;
    }
}

namespace {
    void udevIoCb(struct ev_loop*, ev_io* w, int) {
        auto* dev = (KmsDevice*)w->data;
        udev_device* d = udev_monitor_receive_device(dev->mon);

        if (!d) {
            return;
        }

        const char* node = udev_device_get_devnode(d);
        bool ours = node && StringView(node) == dev->path.view();

        udev_device_unref(d);

        if (ours && dev->output) {
            dev->output->hotplug();
        }
    }
}

void KmsOutput::hotplug() {
    drmModeConnector* conn = drmModeGetConnector(fd, connectorId);

    if (!conn) {
        return;
    }

    bool connected = conn->connection == DRM_MODE_CONNECTED;

    drmModeFreeConnector(conn);

    if (connected == connectorConnected) {
        return;
    }

    connectorConnected = connected;

    if (!connected) {
        sysO << "imway: connector disconnected"_sv << endL;

        return;
    }

    flipPending = false;

    u32 lastFb = scanCount > 0 ? scan[scanNext ^ 1].fbId : bufs[nextBuf ^ 1].fbId;

    if (modesetDone && commit(lastFb, true)) {
        sysO << "imway: connector reconnected, remodeset"_sv << endL;
    }
}

KmsOutput::KmsOutput(int drmFd, const DeviceVk& v, Session& session, const char* connector, const char* modeStr, double hdrWhiteNits) : fd(drmFd), vk(v), hdrNits(hdrWhiteNits) {
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

    // gamma is useful without hdr too (night light), grab it up front
    crtcGammaProp = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "GAMMA_LUT");
    gamLutSize = getPropValue(fd, crtcId, DRM_MODE_OBJECT_CRTC, "GAMMA_LUT_SIZE", 0);

    if (cursorPlaneId) {
        try {
            u64 cw = 0, ch = 0;

            if (drmGetCap(fd, DRM_CAP_CURSOR_WIDTH, &cw) != 0 || !cw) {
                cw = 64;
            }

            if (drmGetCap(fd, DRM_CAP_CURSOR_HEIGHT, &ch) != 0 || !ch) {
                ch = 64;
            }

            createDumb(cursorBuf, (u32)cw, (u32)ch, DRM_FORMAT_ARGB8888);

            cuFbId = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "FB_ID");
            cuCrtcId = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
            cuSrcX = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "SRC_X");
            cuSrcY = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "SRC_Y");
            cuSrcW = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "SRC_W");
            cuSrcH = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "SRC_H");
            cuCrtcX = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_X");
            cuCrtcY = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
            cuCrtcW = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_W");
            cuCrtcH = getPropId(fd, cursorPlaneId, DRM_MODE_OBJECT_PLANE, "CRTC_H");

            curW = (int)cw;
            curH = (int)ch;
            sysO << "imway: cursor plane "_sv << cursorPlaneId << ", "_sv << curW << "x"_sv << curH << endL;
        } catch (...) {
            sysE << "imway: cursor plane setup failed: "_sv << Exception::current() << ", software cursor"_sv << endL;
            cursorPlaneId = 0;
        }
    }

    drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &modeBlob);

    u32 scanFourcc = DRM_FORMAT_XRGB8888;
    VkFormat scanVk = kVkFormat;

    if (hdrNits > 0) {
        if (setupHdr()) {
            hdrActive = true;
            scanFourcc = DRM_FORMAT_XRGB2101010;
            scanVk = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
            sysO << "imway: HDR output: BT.2020 + PQ, sdr white "_sv << hdrNits << " nits"_sv << endL;
        } else {
            sysE << "imway: HDR unsupported here, staying SDR"_sv << endL;
            hdrNits = 0;
        }
    }

    if (vk.hasDmabuf) {
        u64 mods[64];
        u32 nmods = planeModifiers(fd, planeId, scanFourcc, mods, 64);

        for (auto& sb : scan) {
            if (createScanBuf(vk, fd, mode.hdisplay, mode.vdisplay, mods, nmods, scanVk, scanFourcc, sb)) {
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
            createDumb(b, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888);
        }
    }

    sysO << "imway: kms output: "_sv << mode.hdisplay << "x"_sv << mode.vdisplay << "@"_sv << mode.vrefresh << ", connector "_sv << connectorId << ", crtc "_sv << crtcId << ", plane "_sv << planeId << endL;
}

KmsOutput::~KmsOutput() noexcept {
    restoreVt();

    for (auto& sb : scan) {
        destroyScanBuf(vk, fd, sb);
    }

    auto freeDumb = [&](DumbBuffer& b) {
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
    };

    for (auto& b : bufs) {
        freeDumb(b);
    }

    freeDumb(cursorBuf);

    if (modeBlob) {
        drmModeDestroyPropertyBlob(fd, modeBlob);
    }

    for (u32 blob : {hdrMetaBlob, degammaBlob, ctmBlob, gammaBlob}) {
        if (blob) {
            drmModeDestroyPropertyBlob(fd, blob);
        }
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
            CStr<64> name;

            connectorName(c, name);
            ok = name.view() == StringView(connector);
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

    for (u32 i = 0; i < planes->count_planes && !(planeId && cursorPlaneId); i++) {
        drmModePlane* p = drmModeGetPlane(fd, planes->planes[i]);

        if (!p) {
            continue;
        }

        if (p->possible_crtcs & (1u << crtcIndex)) {
            u32 typeProp = getPropId(fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type");
            drmModeObjectProperties* pp = drmModeObjectGetProperties(fd, p->plane_id, DRM_MODE_OBJECT_PLANE);

            for (u32 j = 0; j < pp->count_props; j++) {
                if (pp->props[j] == typeProp) {
                    if (pp->prop_values[j] == DRM_PLANE_TYPE_PRIMARY && !planeId) {
                        planeId = p->plane_id;
                    } else if (pp->prop_values[j] == DRM_PLANE_TYPE_CURSOR && !cursorPlaneId) {
                        cursorPlaneId = p->plane_id;
                    }
                }
            }

            drmModeFreeObjectProperties(pp);
        }

        drmModeFreePlane(p);
    }

    drmModeFreePlaneResources(planes);
    STD_VERIFY(planeId);
}

// SDR desktop on an HDR display: the whole transform runs on the DCN scanout
// pipeline, the renderer keeps producing plain sRGB. DEGAMMA_LUT decodes sRGB
// to linear, CTM rotates BT.709 primaries into BT.2020, GAMMA_LUT maps linear
// [0..1] to PQ with 1.0 pinned at hdrNits — that knob is the macOS-style
// "sdr white" brightness
bool KmsOutput::setupHdr() {
    if (!getEnumProp(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "Colorspace", "BT2020_RGB", &connColorspace, &colorspaceBt2020)) {
        sysE << "imway: hdr: connector has no Colorspace/BT2020_RGB"_sv << endL;

        return false;
    }

    connHdrMeta = getPropId(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "HDR_OUTPUT_METADATA");
    crtcDegammaProp = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "DEGAMMA_LUT");
    crtcCtmProp = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "CTM");

    u64 degSize = getPropValue(fd, crtcId, DRM_MODE_OBJECT_CRTC, "DEGAMMA_LUT_SIZE", 0);

    if (!connHdrMeta || !crtcDegammaProp || !crtcCtmProp || !crtcGammaProp || !degSize || !gamLutSize) {
        sysE << "imway: hdr: missing kms props (meta "_sv << connHdrMeta << ", degamma "_sv << crtcDegammaProp << "/"_sv << degSize << ", ctm "_sv << crtcCtmProp << ", gamma "_sv << crtcGammaProp << "/"_sv << gamLutSize << ")"_sv << endL;

        return false;
    }

    u64 mods[64];

    if (planeModifiers(fd, planeId, DRM_FORMAT_XRGB2101010, mods, 64) == 0) {
        sysE << "imway: hdr: primary plane has no XRGB2101010"_sv << endL;

        return false;
    }

    hdr_output_metadata meta{};

    meta.metadata_type = 0;
    meta.hdmi_metadata_type1.metadata_type = 0;
    meta.hdmi_metadata_type1.eotf = 2; // SMPTE ST 2084 (PQ)
    // BT.2020 primaries in 0.00002 units, R/G/B then D65 white
    meta.hdmi_metadata_type1.display_primaries[0] = {35400, 14600};
    meta.hdmi_metadata_type1.display_primaries[1] = {8500, 39850};
    meta.hdmi_metadata_type1.display_primaries[2] = {6550, 2300};
    meta.hdmi_metadata_type1.white_point = {15635, 16450};
    meta.hdmi_metadata_type1.max_display_mastering_luminance = 1000; // 1 nit units
    meta.hdmi_metadata_type1.min_display_mastering_luminance = 1;    // 0.0001 nit units
    meta.hdmi_metadata_type1.max_cll = 1000;
    meta.hdmi_metadata_type1.max_fall = 400;
    drmModeCreatePropertyBlob(fd, &meta, sizeof(meta), &hdrMetaBlob);

    Vector<drm_color_lut> lut;

    lut.zero((size_t)degSize);

    for (u64 i = 0; i < degSize; i++) {
        double x = (double)i / (double)(degSize - 1);
        double lin = x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
        u16 v = (u16)(lin * 65535.0 + 0.5);

        lut.mut(i) = {v, v, v, 0};
    }

    drmModeCreatePropertyBlob(fd, lut.data(), (u32)(degSize * sizeof(drm_color_lut)), &degammaBlob);

    // BT.709 -> BT.2020, S31.32 fixed point (all coefficients positive)
    static const double m709to2020[9] = {0.627404, 0.329283, 0.043313, 0.069097, 0.919540, 0.011362, 0.016391, 0.088013, 0.895595};
    drm_color_ctm ctm{};

    for (int i = 0; i < 9; i++) {
        ctm.matrix[i] = (u64)(m709to2020[i] * 4294967296.0 + 0.5);
    }

    drmModeCreatePropertyBlob(fd, &ctm, sizeof(ctm), &ctmBlob);

    buildGammaLut();

    return hdrMetaBlob && degammaBlob && ctmBlob && gammaBlob;
}

// Tanner Helland's fit of the black body curve; only the warm (< 6500K)
// side matters here, red stays at 1.0
static void tempToRgb(double k, double& r, double& g, double& b) {
    double t = k / 100.0;

    r = 1.0;
    g = (99.4708025861 * log(t) - 161.1195681661) / 255.0;
    b = t <= 19.0 ? 0.0 : (138.5177312231 * log(t - 10.0) - 305.0447927307) / 255.0;
    g = g < 0 ? 0 : g > 1 ? 1 : g;
    b = b < 0 ? 0 : b > 1 ? 1 : b;
}

// the single GAMMA_LUT carries both knobs: linear [0..1] -> PQ with 1.0
// pinned at hdrNits when the hdr pipeline is up, a plain srgb-encoded ramp
// otherwise, both tinted by the night light temperature; with everything
// neutral the blob is dropped (0 = kernel passthrough)
void KmsOutput::buildGammaLut() {
    if (!crtcGammaProp || !gamLutSize) {
        return;
    }

    if (gammaBlob) {
        drmModeDestroyPropertyBlob(fd, gammaBlob);
        gammaBlob = 0;
    }

    double mr = 1, mg = 1, mb = 1;

    if (tempK > 0) {
        tempToRgb(tempK, mr, mg, mb);
    }

    if (!hdrActive && tempK <= 0) {
        return;
    }

    Vector<drm_color_lut> lut;

    lut.zero((size_t)gamLutSize);

    for (u64 i = 0; i < gamLutSize; i++) {
        double x = (double)i / (double)(gamLutSize - 1);

        if (hdrActive) {
            auto pq = [](double y) {
                double ym = pow(y, 0.1593017578125);

                return pow((0.8359375 + 18.8515625 * ym) / (1.0 + 18.6875 * ym), 78.84375);
            };
            double y = x * hdrNits / 10000.0;

            lut.mut(i) = {(u16)(pq(y * mr) * 65535.0 + 0.5), (u16)(pq(y * mg) * 65535.0 + 0.5), (u16)(pq(y * mb) * 65535.0 + 0.5), 0};
        } else {
            // redshift-style: scale the encoded ramp, close enough for a tint
            lut.mut(i) = {(u16)(x * mr * 65535.0 + 0.5), (u16)(x * mg * 65535.0 + 0.5), (u16)(x * mb * 65535.0 + 0.5), 0};
        }
    }

    drmModeCreatePropertyBlob(fd, lut.data(), (u32)(gamLutSize * sizeof(drm_color_lut)), &gammaBlob);
}

void KmsOutput::createDumb(DumbBuffer& b, u32 w, u32 h, u32 format) {
    drm_mode_create_dumb create{};

    create.width = w;
    create.height = h;
    create.bpp = 32;
    STD_VERIFY(drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) == 0);

    b.handle = create.handle;
    b.pitch = create.pitch;
    b.size = create.size;

    u32 handles[4] = {b.handle}, pitches[4] = {b.pitch}, offsets[4] = {};

    STD_VERIFY(drmModeAddFB2(fd, w, h, format, handles, pitches, offsets, &b.fbId, 0) == 0);

    drm_mode_map_dumb mapReq{};

    mapReq.handle = b.handle;
    STD_VERIFY(drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mapReq) == 0);

    b.map = (u8*)mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)mapReq.offset);
    STD_VERIFY(b.map != MAP_FAILED);
}

int KmsOutput::tryCommit(u32 fbId, bool doModeset, bool withCursor) {
    drmModeAtomicReq* req = drmModeAtomicAlloc();

    if (doModeset) {
        drmModeAtomicAddProperty(req, connectorId, connCrtcId, crtcId);
        drmModeAtomicAddProperty(req, crtcId, crtcModeId, modeBlob);
        drmModeAtomicAddProperty(req, crtcId, crtcActive, 1);

        if (hdrActive) {
            drmModeAtomicAddProperty(req, connectorId, connColorspace, colorspaceBt2020);
            drmModeAtomicAddProperty(req, connectorId, connHdrMeta, hdrMetaBlob);
            drmModeAtomicAddProperty(req, crtcId, crtcDegammaProp, degammaBlob);
            drmModeAtomicAddProperty(req, crtcId, crtcCtmProp, ctmBlob);
            drmModeAtomicAddProperty(req, crtcId, crtcGammaProp, gammaBlob);
        } else if (gammaBlob && crtcGammaProp) {
            // night light without the hdr pipeline
            drmModeAtomicAddProperty(req, crtcId, crtcGammaProp, gammaBlob);
        }
    }

    // live lut change (sdr white / night light): no modeset needed, a zero
    // blob resets the crtc to passthrough
    if (crtcGammaProp && gammaDirty && !doModeset) {
        drmModeAtomicAddProperty(req, crtcId, crtcGammaProp, gammaBlob);
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

    if (cursorPlaneId && cursorEnabled) {
        if (withCursor) {
            addCursorProps(req);
        } else {
            // shut the plane off explicitly, otherwise the kernel keeps
            // scanning out the last cursor state — a frozen cursor on screen
            drmModeAtomicAddProperty(req, cursorPlaneId, cuFbId, 0);
            drmModeAtomicAddProperty(req, cursorPlaneId, cuCrtcId, 0);
        }
    }

    u32 flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;

    if (doModeset) {
        flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
    }

    int ret = drmModeAtomicCommit(fd, req, flags, this);

    drmModeAtomicFree(req);

    if (ret == 0) {
        gammaDirty = false;
    }

    return ret == 0 ? 0 : errno;
}

bool KmsOutput::commit(u32 fbId, bool doModeset) {
    int err = tryCommit(fbId, doModeset, true);

    if (err == EBUSY) {
        return false;
    }

    // bisect: some driver/mode combinations reject the cursor plane in the
    // same commit — retry without it and fall back to the software cursor
    if (err != 0 && cursorPlaneId && cursorEnabled) {
        int errNoCursor = tryCommit(fbId, doModeset, false);

        if (errNoCursor == 0) {
            sysE << "imway: cursor plane rejected by this mode (errno "_sv << err << "), software cursor"_sv << endL;
            cursorPlaneId = 0;
            curW = curH = 0;
            cursorEnabled = false;

            flipPending = true;

            return true;
        }

        err = errNoCursor;
    }

    if (err != 0) {
        sysE << "imway: kms atomic commit failed, errno "_sv << err << (doModeset ? " (modeset)"_sv : ""_sv) << endL;

        return false;
    }

    flipPending = true;

    return true;
}

// the cursor plane rides the per-frame atomic commit and nothing else:
// standalone nonblocking commits occupy the CRTC till vblank and starve
// frame commits with EBUSY, and the legacy SetCursor2/MoveCursor fast path
// divides by zero in drm_crtc_next_vblank_start on 7.x PREEMPT_RT kernels
void KmsOutput::addCursorProps(drmModeAtomicReq* req) {
    bool on = cursorVisible;

    drmModeAtomicAddProperty(req, cursorPlaneId, cuFbId, on ? cursorBuf.fbId : 0);
    drmModeAtomicAddProperty(req, cursorPlaneId, cuCrtcId, on ? crtcId : 0);

    if (on) {
        drmModeAtomicAddProperty(req, cursorPlaneId, cuSrcX, 0);
        drmModeAtomicAddProperty(req, cursorPlaneId, cuSrcY, 0);
        drmModeAtomicAddProperty(req, cursorPlaneId, cuSrcW, (u64)curW << 16);
        drmModeAtomicAddProperty(req, cursorPlaneId, cuSrcH, (u64)curH << 16);
        drmModeAtomicAddProperty(req, cursorPlaneId, cuCrtcX, (u64)(i64)cursorX);
        drmModeAtomicAddProperty(req, cursorPlaneId, cuCrtcY, (u64)(i64)cursorY);
        drmModeAtomicAddProperty(req, cursorPlaneId, cuCrtcW, (u64)curW);
        drmModeAtomicAddProperty(req, cursorPlaneId, cuCrtcH, (u64)curH);
    }
}

void KmsOutput::setCursorImage(const u32* argb) {
    if (!curW) {
        return;
    }

    for (int y = 0; y < curH; y++) {
        memcpy(cursorBuf.map + (size_t)y * cursorBuf.pitch, argb + (size_t)y * curW, (size_t)curW * 4);
    }

    cursorEnabled = true;
}

void KmsOutput::setCursorPos(int x, int y, bool visible) {
    if (!curW || !cursorEnabled) {
        return;
    }

    cursorX = x;
    cursorY = y;
    cursorVisible = visible;
}

void KmsOutput::setPowerSave(bool on) {
    if (!modesetDone || !sessionActive || on == powered) {
        return;
    }

    powered = on;

    if (on) {
        flipPending = false;

        u32 lastFb = scanCount > 0 ? scan[scanNext ^ 1].fbId : bufs[nextBuf ^ 1].fbId;

        if (commit(lastFb, true)) {
            sysO << "imway: display back on"_sv << endL;
        }
    } else {
        // blocking commit, just drops ACTIVE; planes keep their state
        drmModeAtomicReq* req = drmModeAtomicAlloc();

        drmModeAtomicAddProperty(req, crtcId, crtcActive, 0);
        drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
        drmModeAtomicFree(req);
        flipPending = false;
        sysO << "imway: display off (idle)"_sv << endL;
    }
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

        // a render node is enough for syncobj ioctls (explicit sync)
        for (int i = 128; i < 136 && syncFd < 0; i++) {
            CStr<64> pth;

            pth << "/dev/dri/renderD"_sv << i;

            int fd = open(pth.cStr(), O_RDWR | O_CLOEXEC);

            if (fd < 0) {
                continue;
            }

            u64 cap = 0;

            if (drmGetCap(fd, DRM_CAP_SYNCOBJ_TIMELINE, &cap) == 0 && cap) {
                syncFd = fd;
                vk.drmFd = fd;
            } else {
                close(fd);
            }
        }
    }
}

HeadlessDevice::~HeadlessDevice() noexcept {
    destroyVulkan(vk);

    if (syncFd >= 0) {
        close(syncFd);
    }
}

::Output* HeadlessDevice::createOutput(const char*, const char* modeStr, double) {
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
        CStr<64> p;

        p << "/dev/dri/card"_sv << i;

        int fd = open(p.cStr(), O_RDWR | O_CLOEXEC | O_NONBLOCK);

        if (fd < 0) {
            continue;
        }

        bool atomic = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0;
        drmVersion* ver = drmGetVersion(fd);

        sysO << p.view() << ": "_sv << (ver && ver->name ? ver->name : "?") << (atomic ? ", atomic"_sv : ", NO atomic (unusable)"_sv) << endL;

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

                CStr<64> name;

                connectorName(conn, name);
                sysO << "  "_sv << name.view() << (conn->connection == DRM_MODE_CONNECTED ? ": connected"_sv : ": disconnected"_sv);

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
