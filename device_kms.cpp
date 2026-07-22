#include "composer.h"
#include "device.h"

#include "device_vk.h"
#include "frame_listener.h"
#include "listener.h"
#include "output.h"
#include "renderer.h"
#include "scene.h"
#include "session.h"
#include "util.h"

#include <ev.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux/i2c-dev.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <libudev.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <std/dbg/verify.h>
#include <std/ios/fs_utils.h>
#include <std/ios/out_fd.h>
#include <std/ios/sys.h>
#include <std/sys/fs.h>
#include <std/sys/fd.h>
#include <std/sys/atomic.h>
#include <std/sys/event_fd.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sym/i_map.h>
#include <std/sys/throw.h>
#include <std/thr/pool.h>
#include "device_kms.h"
#include "intr_list.h"
#include "pooled.h"
#include "pooled_ev.h"
#include "pooled_fd.h"

using namespace stl;

namespace {
    void connectorName(const drmModeConnector* c, StringBuilder& out) {
        const char* t = drmModeGetConnectorTypeName(c->connector_type);

        out << (t ? t : "Unknown") << "-"_sv << c->connector_type_id;
    }

    struct ScanBuf {
        ScanoutBuffer pub;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        u64 allocationSize = 0;
        u64 modifier = 0;
        u32 offset = 0;
        u32 stride = 0;
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

    bool getRangeProp(int fd, u32 objId, u32 objType, const char* name,
                      u32* propId, u64* min, u64* max) {
        drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objId, objType);

        if (!props) {
            return false;
        }

        bool found = false;

        for (u32 i = 0; i < props->count_props && !found; i++) {
            drmModePropertyRes* p = drmModeGetProperty(fd, props->props[i]);

            if (p && StringView(p->name) == StringView(name) &&
                (p->flags & DRM_MODE_PROP_RANGE) && p->count_values >= 2) {
                *propId = p->prop_id;
                *min = p->values[0];
                *max = p->values[1];
                found = true;
            }

            if (p) drmModeFreeProperty(p);
        }

        drmModeFreeObjectProperties(props);
        return found;
    }

    bool readEdidColorCapabilities(int fd, u32 connectorId,
                                   DisplayColorCapabilities& capabilities) {
        u64 blobId = getPropValue(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR,
                                  "EDID", 0);
        drmModePropertyBlobRes* blob = blobId ?
            drmModeGetPropertyBlob(fd, (u32)blobId) : nullptr;

        if (!blob) {
            return false;
        }

        bool ok = parseEdidColorCapabilities(blob->data, blob->length, capabilities);
        drmModeFreePropertyBlob(blob);
        return ok;
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

            constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

            if ((m.drmFormatModifierTilingFeatures & needed) != needed) {
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
            ifi.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT;

            VkExternalImageFormatProperties extProps{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
            VkImageFormatProperties2 iprops{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};

            iprops.pNext = &extProps;

            if (vkGetPhysicalDeviceImageFormatProperties2(vk.phys, &ifi, &iprops) != VK_SUCCESS) {
                continue;
            }

            constexpr VkExternalMemoryFeatureFlags neededExternal =
                VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;

            if ((extProps.externalMemoryProperties.externalMemoryFeatures & neededExternal) != neededExternal) {
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
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
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
        sb.allocationSize = req.size;
        sb.modifier = chosen.drmFormatModifier;
        sb.offset = (u32)layout.offset;
        sb.stride = (u32)layout.rowPitch;

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

    struct KmsOutput;

    struct CallKmsSessionEnabled: Listener {
        KmsOutput* parent;

        CallKmsSessionEnabled(KmsOutput* p);
        void onListen(void*) override;
    };

    struct CallKmsSessionDisabled: Listener {
        KmsOutput* parent;

        CallKmsSessionDisabled(KmsOutput* p);
        void onListen(void*) override;
    };

    struct DirectFbOwner {
        KmsOutput* output = nullptr;
        DmabufBuffer* buffer = nullptr;

        DirectFbOwner(KmsOutput* o, DmabufBuffer* b);
        ~DirectFbOwner() noexcept;
    };

    struct KmsOutput: public ::Output {
        Composer* c = nullptr;
        int fd = -1;
        int ttyFd = -1;
        // int, not long: the kernel writes exactly 4 bytes here and a
        // big-endian long would keep the value in the wrong half
        int oldKbMode = -1;
        bool sessionActive = true;
        const DeviceVk* vk = nullptr;

        // the link-configuration arena: guards on the scanout slots (and
        // the screenshot replacement slot) registered when the swapchain
        // comes up; slot contents churn during screenshot handoffs, so the
        // guards destroy whatever the slot holds when the link dies
        ObjPool* link = nullptr;
        ScanBuf scan[2];
        int scanCount = 0;
        int scanNext = 0;
        int currentScan = -1;
        int queuedScan = -1;
        VkFormat scanFormat = VK_FORMAT_UNDEFINED;
        u32 scanFourcc = 0;
        u64 scanMods[64] = {};
        u32 scanModCount = 0;

        ScanBuf screenshotReplacement;
        int screenshotState = 0;
        int screenshotResult = 0;
        int screenshotIndex = -1;
        bool screenshotWasPresented = false;
        Listener* screenshotReady = nullptr;
        EventFD screenshotDone;
        ev_io* screenshotIo = nullptr;

        u32 connectorId = 0;
        StringBuilder connectorLabel;
        int physicalWmm = 0, physicalHmm = 0;
        u32 crtcId = 0;
        u32 planeId = 0;
        drmModeModeInfo mode{};
        u32 modeBlob = 0;

        u32 connCrtcId = 0;
        u32 crtcModeId = 0, crtcActive = 0;
        u32 plFbId = 0, plCrtcId = 0;
        u32 plInFenceFd = 0;
        u32 plSrcX = 0, plSrcY = 0, plSrcW = 0, plSrcH = 0;
        u32 plCrtcX = 0, plCrtcY = 0, plCrtcW = 0, plCrtcH = 0;

        // sysfs backlight of the internal panel; empty path = none
        StringBuilder blPath;
        long blMax = 0;

        // ddc/ci brightness for external monitors (VCP 0x10 over the
        // connector's i2c bus); writes coalesce through ddcTimer because the
        // link needs ~50ms between transactions and a slider drags faster
        ObjPool* pool = nullptr;
        struct ev_loop* loop = nullptr;
        int ddcFd = -1;

        // imported drm framebuffers for direct-scanout client dmabufs,
        // keyed by the DmabufBuffer pointer; freed when the buffer dies
        struct DirectFb {
            DmabufBuffer* buf = nullptr;
            u32 fb = 0;
            u32 handles[4] = {};
            int nh = 0;
            DirectFbOwner* owner = nullptr;
        };
        Vector<DirectFb> directFbs;

        // GEM handles are per-BO on a drm fd, not per-import: two wl_buffers
        // sharing one BO resolve to the same numeric handle, and GEM_CLOSE
        // drops it for both. Refcount handle -> uses, close on the last unref.
        IntMap<int> gemHandles;

        DmabufBuffer* queuedDirect = nullptr;
        DmabufBuffer* currentDirect = nullptr;
        FrameResource* queuedDirectFrame = nullptr;
        FrameResource* currentDirectFrame = nullptr;
        bool asyncFlipCap = false;  // DRM_CAP_ASYNC_PAGE_FLIP
        bool tearingAllowed = false;
        long ddcMax = 0;
        int ddcCur = 0;
        int ddcPending = -1;
        bool ddcTimerOn = false;
        ev_timer* ddcTimer = nullptr;

        OutputColorState color;
        OutputConfiguration config;
        DisplayColorCapabilities displayCapabilities;
        u32 connColorspace = 0, connHdrMeta = 0;
        u64 colorspaceBt2020 = 0, colorspaceDefault = 0;
        u32 connMaxBpc = 0, connRange = 0, connLinkBpc = 0;
        u64 maxBpcValue = 0, rangeValue = 0;
        u64 rangeFullValue = 0, rangeLimitedValue = 0;
        bool signalFeedbackLogged = false;
        u32 crtcDegammaProp = 0, crtcCtmProp = 0, crtcGammaProp = 0;
        HdrOutputMetadata metadata;
        u32 hdrMetaBlob = 0;
        u32 pendingHdrMetaBlob = 0;
        bool hdrMetaDirty = false;
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

        KmsOutput(Composer& c, int drmFd, const DeviceVk* v, StringView connector,
                  StringView modeStr, const OutputConfiguration& config);

        void sessionEnabled();
        void sessionDisabled();
        ~KmsOutput() noexcept;

        int width() const override;
        int height() const override;
        double refresh() const override;
        StringView outputName() const override;
        StringView make() const override;
        StringView model() const override;
        int physicalWidthMm() const override;
        int physicalHeightMm() const override;
        void pickPipe(StringView connector, StringView modeStr);
        bool setupHdr();
        bool createHdrMetadataBlob(const HdrOutputMetadata& metadata, u32& blob);
        void applyPendingHdrMetadata();

        const OutputColorState& colorState() const override;
        void setSdrWhite(double nits) override;
        const HdrOutputMetadata& hdrMetadata() const override;
        void setHdrMetadata(const HdrOutputMetadata& metadata) override;
        void setColorTemp(double kelvin) override;
        double colorTemp() const override;
        bool lastFlip(u64& nsec, u32& seq) const override;
        void createDumb(DumbBuffer& b, u32 w, u32 h, u32 format);
        int tryCommit(u32 fbId, bool doModeset, bool withCursor,
                      int inFenceFd = -1, bool testOnly = false);
        void drainPendingFlip();
        bool commit(u32 fbId, bool doModeset, int inFenceFd = -1);
        void updateSignalFeedback();
        void addCursorProps(drmModeAtomicReq* req);

        int cursorCapW() const override;
        int cursorCapH() const override;
        void setCursorImage(const u32* argb) override;
        void setCursorPos(int x, int y, bool visible) override;
        void setPowerSave(bool on) override;

        void initBacklight();
        void initDdc(StringView connName);
        bool ddcGet(u8 vcp, int& cur, int& max);
        void ddcSet(u8 vcp, int val);
        bool hasBrightness() const override;
        float brightness() const override;
        void setBrightness(float v) override;
        void setupVt();
        void restoreVt() noexcept;

        bool start() override;

        bool ready() const override;
        void hotplug();
        bool vsynced() const override;
        int scanoutCount() const override;
        ScanoutBuffer* scanoutBuffer(int i) override;
        int acquire() override;
        bool supportsRenderFence() const override;
        bool presentImage(int i, int renderFenceFd) override;
        bool prepareScreenshot(Listener& ready) override;
        bool takeScreenshot(int i, SharedScanout& image) override;
        bool screenshotPending() const override;
        void screenshotPrepared();
        void retireScreenshot();
        bool presentNeedsPixels() const override;
        bool directScanout(DmabufBuffer*, FrameResource*) override;
        void dropScanoutFb(DmabufBuffer*) override;
        void setTearingHint(bool allow) override { tearingAllowed = allow && asyncFlipCap; }
        void scanoutFormatsImpl(stl::VisitorFace&& vis) override;
        u32 importDirectFb(DmabufBuffer* buf);
        void gemHandleRef(u32 handle);
        void gemHandleUnref(u32 handle);
        void releaseDirectUse(DmabufBuffer*& buf, FrameResource*& frame);
        void present(const void* pixels) override;
    };

    CallKmsSessionEnabled::CallKmsSessionEnabled(KmsOutput* p)
        : parent(p)
    {
    }

    void CallKmsSessionEnabled::onListen(void*) {
        parent->sessionEnabled();
    }

    CallKmsSessionDisabled::CallKmsSessionDisabled(KmsOutput* p)
        : parent(p)
    {
    }

    void CallKmsSessionDisabled::onListen(void*) {
        parent->sessionDisabled();
    }

    void pageFlipHandler(int, unsigned seq, unsigned sec, unsigned usec, void* data) {
        auto* out = (KmsOutput*)data;

        out->flipPending = false;
        out->currentScan = out->queuedScan;
        out->queuedScan = -1;
        out->flipNs = (u64)sec * 1000000000ull + (u64)usec * 1000ull;
        out->flipSeq = seq;
        out->releaseDirectUse(out->currentDirect, out->currentDirectFrame);
        out->currentDirect = out->queuedDirect;
        out->currentDirectFrame = out->queuedDirectFrame;
        out->queuedDirect = nullptr;
        out->queuedDirectFrame = nullptr;
        out->retireScreenshot();
        out->updateSignalFeedback();

        u32 msec = (u32)(out->flipNs / 1000000ull);

        FrameEvent event{msec};

        forEach<Listener>(out->c->frameListeners, [&event](Listener& listener) {
            listener.onListen(&event);
        });
    }

    void drmIoCb(struct ev_loop*, ev_io* w, int) {
        drmEventContext ctx{};

        ctx.version = 2;
        ctx.page_flip_handler = pageFlipHandler;
        drmHandleEvent((int)(intptr_t)w->data, &ctx);
    }

    void udevIoCb(struct ev_loop*, ev_io* w, int);

    void screenshotDoneCb(struct ev_loop*, ev_io* w, int) {
        ((KmsOutput*)w->data)->screenshotPrepared();
    }

    struct KmsDevice: public Device {
        Composer* c = nullptr;
        ObjPool* pool = nullptr;
        struct ev_loop* loop = nullptr;
        Session* session = nullptr;
        int fd = -1;
        StringBuilder path;
        DeviceVk* vk = nullptr;
        Vector<DmabufFormat> formats;
        udev* ud = nullptr;
        udev_monitor* mon = nullptr;
        KmsOutput* output = nullptr;

        // wp-drm-lease: the connector id the desktop output drives, so it is
        // never offered for lease
        u32 outputConnectorId = 0;

        KmsDevice(Composer& comp, StringView devPath);

        int drmFd() const override;
        bool explicitSyncSupported() const override;
        unsigned long long renderDevice() const override;
        void dmabufFormatsImpl(VisitorFace&& vis) override;
        void leaseConnectorsImpl(VisitorFace&& vis) override;
        int createLease(const u32* connectorIds, int count, u32& lesseeId) override;
        void revokeLease(u32 lesseeId) override;
        ::Output* createOutput(StringView connector, StringView modeStr,
                               const OutputConfiguration& config) override;
        Renderer* createRenderer(Composer& c, StringView fontPath, float uiScale, int framesLimit) override;
    };

    int openKmsNode(Session& session, StringView devPath, StringBuilder& outPath) {
        if (!devPath.empty()) {
            int fd = session.openDevice(Buffer(devPath).cStr());

            if (fd < 0) {
                Errno(-fd).raise(StringBuilder() << "kms: open "_sv << devPath);
            }

            outPath << devPath;

            return fd;
        }

        for (int i = 0; i < 8; i++) {
            auto& p = sb();

            p << "/dev/dri/card"_sv << i;

            int fd = session.openDevice(p.cStr());

            if (fd < 0) {
                continue;
            }

            if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
                session.closeDevice(fd);

                continue;
            }

            outPath << sv(p);

            return fd;
        }

        Errno().raise(StringBuilder() << "kms: no device with atomic support under /dev/dri"_sv);

        return -1;
    }
}

KmsDevice::KmsDevice(Composer& comp, StringView devPath)
    : c(&comp)
    , pool(comp.pool)
    , loop(comp.loop)
    , session(comp.session)
{
    fd = openKmsNode(*session, devPath, path);
    pooledSessionFD(*pool, *session, fd);

    STD_VERIFY(drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) == 0);
    STD_VERIFY(drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0);

    vk = pool->make<DeviceVk>(fd);

    if (vk->hasDmabuf) {
        vk->queryDmabufFormats([this](const DmabufFormat& f) { formats.pushBack(f); });
    }

    ev_io* drmIo = createEvIo(*pool, loop);

    ev_io_init(drmIo, drmIoCb, fd, EV_READ);
    drmIo->data = (void*)(intptr_t)fd;
    ev_io_start(loop, drmIo);

    ud = udev_new();

    if (ud) {
        udev* held = ud;

        pooledGuard(*pool, [held] {
            udev_unref(held);
        });

        // "kernel" = raw uevents, works with or without a running udevd
        mon = udev_monitor_new_from_netlink(ud, "kernel");
    }

    if (mon) {
        udev_monitor* held = mon;

        pooledGuard(*pool, [held] {
            udev_monitor_unref(held);
        });
        udev_monitor_filter_add_match_subsystem_devtype(mon, "drm", nullptr);
        udev_monitor_enable_receiving(mon);
        ev_io* udevIo = createEvIo(*pool, loop);

        ev_io_init(udevIo, udevIoCb, udev_monitor_get_fd(mon), EV_READ);
        udevIo->data = this;
        ev_io_start(loop, udevIo);
    }

    sysO << "imway: device "_sv << sv(path) << endL;
}

int KmsDevice::drmFd() const {
    return fd;
}

bool KmsDevice::explicitSyncSupported() const {
    return fd >= 0 && vk && vk->hasSyncFd;
}

unsigned long long KmsDevice::renderDevice() const {
    return vk->renderDev;
}

void KmsDevice::dmabufFormatsImpl(VisitorFace&& vis) {
    for (const DmabufFormat& f : formats) {
        vis.visit((void*)&f);
    }
}

namespace {
    // read the "non-desktop" connector property (VR headsets, dedicated
    // panels set it to 1); such connectors are never driven as outputs and
    // are the only ones offered for lease
    bool connectorNonDesktop(int fd, u32 connectorId) {
        drmModeObjectProperties* props =
            drmModeObjectGetProperties(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR);

        if (!props) {
            return false;
        }

        bool nonDesktop = false;

        for (u32 i = 0; i < props->count_props; i++) {
            drmModePropertyRes* p = drmModeGetProperty(fd, props->props[i]);

            if (!p) {
                continue;
            }

            if (StringView(p->name) == "non-desktop"_sv && props->prop_values[i]) {
                nonDesktop = true;
            }

            drmModeFreeProperty(p);
        }

        drmModeFreeObjectProperties(props);

        return nonDesktop;
    }
}

void KmsDevice::leaseConnectorsImpl(VisitorFace&& vis) {
    drmModeRes* res = drmModeGetResources(fd);

    if (!res) {
        return;
    }

    for (int i = 0; i < res->count_connectors; i++) {
        u32 id = res->connectors[i];

        if (id == outputConnectorId || !connectorNonDesktop(fd, id)) {
            continue;
        }

        drmModeConnector* conn = drmModeGetConnector(fd, id);

        if (!conn) {
            continue;
        }

        // the name lives in the shared scratch: vis.visit is synchronous, the
        // wayland bind copies it into the connector events right here
        auto& name = sb();

        connectorName(conn, name);

        LeaseConnector lc;

        lc.connectorId = id;
        lc.name = sv(name);
        lc.description = sv(name);
        vis.visit((void*)&lc);
        drmModeFreeConnector(conn);
    }

    drmModeFreeResources(res);
}

int KmsDevice::createLease(const u32* connectorIds, int count, u32& lesseeId) {
    if (count <= 0) {
        return -EINVAL;
    }

    // each leased connector needs a crtc and its planes; pick free ones,
    // avoiding the crtc the desktop output drives
    Vector<u32> objects;

    drmModeRes* res = drmModeGetResources(fd);

    if (!res) {
        return -ENODEV;
    }

    u32 usedCrtc = output ? output->crtcId : 0;
    Vector<u32> takenCrtcs;

    if (usedCrtc) {
        takenCrtcs.pushBack(usedCrtc);
    }

    bool ok = true;

    for (int i = 0; i < count && ok; i++) {
        objects.pushBack(connectorIds[i]);

        drmModeConnector* conn = drmModeGetConnector(fd, connectorIds[i]);

        if (!conn) {
            ok = false;
            break;
        }

        u32 crtc = 0;

        for (int e = 0; e < conn->count_encoders && !crtc; e++) {
            drmModeEncoder* enc = drmModeGetEncoder(fd, conn->encoders[e]);

            if (!enc) {
                continue;
            }

            for (int c = 0; c < res->count_crtcs; c++) {
                u32 cand = res->crtcs[c];

                if (!(enc->possible_crtcs & (1u << c))) {
                    continue;
                }

                bool taken = false;

                for (u32 t : takenCrtcs) {
                    taken = taken || t == cand;
                }

                if (!taken) {
                    crtc = cand;
                    break;
                }
            }

            drmModeFreeEncoder(enc);
        }

        drmModeFreeConnector(conn);

        if (!crtc) {
            ok = false;
            break;
        }

        takenCrtcs.pushBack(crtc);
        objects.pushBack(crtc);

        drmModePlaneRes* planes = drmModeGetPlaneResources(fd);

        if (planes) {
            for (u32 p = 0; p < planes->count_planes; p++) {
                drmModePlane* pl = drmModeGetPlane(fd, planes->planes[p]);

                u32 crtcIdx = 0;

                for (int ci = 0; ci < res->count_crtcs; ci++) {
                    if (res->crtcs[ci] == crtc) {
                        crtcIdx = (u32)ci;
                    }
                }

                if (pl && (pl->possible_crtcs & (1u << crtcIdx))) {
                    objects.pushBack(planes->planes[p]);
                }

                if (pl) {
                    drmModeFreePlane(pl);
                }
            }

            drmModeFreePlaneResources(planes);
        }
    }

    drmModeFreeResources(res);

    if (!ok) {
        return -EINVAL;
    }

    u32 lessee = 0;
    int leaseFd = drmModeCreateLease(fd, objects.data(), (int)objects.length(), O_CLOEXEC, &lessee);

    if (leaseFd < 0) {
        return leaseFd;
    }

    lesseeId = lessee;

    return leaseFd;
}

void KmsDevice::revokeLease(u32 lesseeId) {
    if (lesseeId) {
        drmModeRevokeLease(fd, lesseeId);
    }
}

::Output* KmsDevice::createOutput(StringView connector, StringView modeStr,
                                  const OutputConfiguration& config) {
    output = pool->make<KmsOutput>(*c, fd, vk, connector, modeStr, config);
    outputConnectorId = output->connectorId;

    return output;
}

Renderer* KmsDevice::createRenderer(Composer& c, StringView fontPath, float uiScale, int framesLimit) {
    return Renderer::create(c, *vk, fontPath, uiScale, framesLimit);
}

namespace {
    void udevIoCb(struct ev_loop*, ev_io* w, int) {
        auto* dev = (KmsDevice*)w->data;
        udev_device* d = udev_monitor_receive_device(dev->mon);

        if (!d) {
            return;
        }

        const char* node = udev_device_get_devnode(d);
        bool ours = node && StringView(node) == sv(dev->path);

        udev_device_unref(d);

        if (ours && dev->output) {
            dev->output->hotplug();
        }
    }
}

// a nonblocking commit may still be in flight when a remodeset is needed;
// blindly clearing flipPending would let the next frame commit hit EBUSY and
// get dropped silently (no page-flip event -> frame callbacks starve until
// the 2s clock tick). Dispatch the pending event the same way drmIoCb would.
void KmsOutput::drainPendingFlip() {
    while (flipPending) {
        pollfd pfd{fd, POLLIN, 0};

        if (poll(&pfd, 1, 100) <= 0) {
            // no event within a frame's worth of time (master dropped, driver
            // glitch): give up so the remodeset below can proceed
            flipPending = false;

            return;
        }

        drmEventContext ctx{};

        ctx.version = 2;
        ctx.page_flip_handler = pageFlipHandler;
        drmHandleEvent(fd, &ctx);
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
        if (connected) {
            signalFeedbackLogged = false;
            updateSignalFeedback();
        }
        return;
    }

    connectorConnected = connected;

    if (!connected) {
        sysO << "imway: connector disconnected"_sv << endL;

        return;
    }

    drainPendingFlip();

    u32 lastFb = scanCount > 0 ? scan[scanNext ^ 1].fbId : bufs[nextBuf ^ 1].fbId;

    if (modesetDone && commit(lastFb, true)) {
        queuedScan = scanCount > 0 ? scanNext ^ 1 : -1;
        sysO << "imway: connector reconnected, remodeset"_sv << endL;
    }
}

KmsOutput::KmsOutput(Composer& c, int drmFd, const DeviceVk* v, StringView connector,
                     StringView modeStr, const OutputConfiguration& outputConfig)
    : c(&c)
    , pool(c.pool)
    , loop(c.loop)
    , fd(drmFd)
    , vk(v)
    , gemHandles(c.pool)
    , config(outputConfig)
{
    c.sessionEnabledListeners.pushBack(c.pool->make<CallKmsSessionEnabled>(this));
    c.sessionDisabledListeners.pushBack(c.pool->make<CallKmsSessionDisabled>(this));
    screenshotIo = createEvIo(*c.pool, c.loop);
    ev_io_init(screenshotIo, screenshotDoneCb, screenshotDone.fd(), EV_READ);
    screenshotIo->data = this;
    ev_io_start(c.loop, screenshotIo);
    pickPipe(connector, modeStr);

    if (!readEdidColorCapabilities(fd, connectorId, displayCapabilities)) {
        sysE << "imway: display EDID unavailable or invalid; color volume requires overrides"_sv << endL;
    }

    color = outputColorState(config, displayCapabilities);

    if (color.hdr() && displayCapabilities.valid &&
        (!displayCapabilities.pq || !displayCapabilities.bt2020Rgb)) {
        sysE << "imway: display EDID does not advertise PQ + BT.2020 RGB"_sv << endL;
        color = OutputColorState::sdr();
    } else if (color.hdr() && !config.displayPeakNits &&
               !displayCapabilities.peakNits) {
        sysE << "imway: display has no HDR peak luminance; using 1000 nit fallback (use --hdr-peak)"_sv << endL;
    }

    HdrContentMetadata initialContent;

    initialContent.add(ColorDescription::sRgb(), color.sdrWhiteNits);
    metadata = hdrOutputMetadata(color, initialContent);

    connCrtcId = getPropId(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    crtcModeId = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    crtcActive = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    plFbId = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "FB_ID");
    plCrtcId = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    plInFenceFd = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "IN_FENCE_FD");
    plSrcX = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_X");
    plSrcY = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    plSrcW = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_W");
    plSrcH = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_H");
    plCrtcX = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    plCrtcY = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    plCrtcW = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    plCrtcH = getPropId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_H");

    // Fetch the whole legacy color pipeline for the startup scrub: KMS color
    // state survives compositor restarts even though our transform is in the
    // output shader.
    crtcGammaProp = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "GAMMA_LUT");
    crtcDegammaProp = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "DEGAMMA_LUT");
    crtcCtmProp = getPropId(fd, crtcId, DRM_MODE_OBJECT_CRTC, "CTM");
    connHdrMeta = getPropId(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "HDR_OUTPUT_METADATA");
    getEnumProp(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "Colorspace", "Default", &connColorspace, &colorspaceDefault);

    u64 minBpc = 0, maxBpc = 0;

    if (getRangeProp(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "max bpc",
                     &connMaxBpc, &minBpc, &maxBpc)) {
        maxBpcValue = color.bpc;

        if (maxBpcValue < minBpc || maxBpcValue > maxBpc) {
            sysE << "imway: requested "_sv << maxBpcValue << " bpc is outside connector range "_sv
                 << minBpc << ".."_sv << maxBpc << endL;
            Errno(EINVAL).raise("invalid --bpc"_sv);
        }
    } else if (config.bpc) {
        sysE << "imway: connector has no max bpc property for explicit --bpc"_sv << endL;
        Errno(ENOTSUP).raise("--bpc unsupported"_sv);
    } else if (color.hdr()) {
        sysE << "imway: connector has no max bpc property; HDR link depth cannot be requested"_sv << endL;
    }

    connLinkBpc = getPropId(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "link bpc");

    u32 rangeProp = 0;
    getEnumProp(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "Broadcast RGB", "Full",
                &rangeProp, &rangeFullValue);
    if (!getEnumProp(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "Broadcast RGB", "Limited 16:235",
                     &rangeProp, &rangeLimitedValue)) {
        getEnumProp(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "Broadcast RGB", "Limited",
                    &rangeProp, &rangeLimitedValue);
    }

    const char* rangeName = config.range == OutputRange::limited ?
        (rangeLimitedValue ? "Limited 16:235" : "Limited") :
        config.range == OutputRange::full ? "Full" : "Automatic";
    getEnumProp(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "Broadcast RGB", rangeName,
                &connRange, &rangeValue);

    if (config.range != OutputRange::automatic && !connRange) {
        sysE << "imway: connector cannot select requested RGB range"_sv << endL;
        Errno(ENOTSUP).raise("--rgb-range unsupported"_sv);
    }

    // atomic async (tearing) page flips for wp-tearing-control
    u64 asyncCap = 0;

    asyncFlipCap = drmGetCap(fd, DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP, &asyncCap) == 0 && asyncCap;

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

    // a silently failed blob would feed MODE_ID=0 into every modeset
    STD_VERIFY(drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &modeBlob) == 0);

    scanFourcc = DRM_FORMAT_XRGB8888;
    scanFormat = kVkFormat;

    if (color.hdr()) {
        if (setupHdr()) {
            scanFourcc = DRM_FORMAT_XRGB2101010;
            scanFormat = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
            sysO << "imway: HDR output: BT.2020 + PQ, target "_sv
                 << color.displayMinNits << ".."_sv << color.displayPeakNits
                 << " nits, maxFALL "_sv << color.displayMaxFallNits
                 << ", sdr white "_sv << color.sdrWhiteNits << " nits"_sv << endL;

            if (cursorPlaneId) {
                // AMD cursor planes consume ARGB8888 outside the primary
                // plane's HDR color path; light cursor pixels consequently
                // arrive black. Composite into XR30 so UI and cursors share
                // the same DEGAMMA/CTM/GAMMA pipeline.
                sysO << "imway: hardware cursor disabled under HDR, software cursor"_sv << endL;
            }
        } else {
            sysE << "imway: HDR unsupported here, staying SDR"_sv << endL;
            color = OutputColorState::sdr();
        }
    }

    // 10 bits for sdr too when the plane takes it: ui gradients and the
    // night-light ramp quantize at framebuffer depth, not lut depth
    if (!color.hdr() && vk->hasDmabuf) {
        u64 m[64];

        if (planeModifiers(fd, planeId, DRM_FORMAT_XRGB2101010, m, 64) > 0) {
            scanFourcc = DRM_FORMAT_XRGB2101010;
            scanFormat = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        }
    }

    if (vk->hasDmabuf) {
        for (;;) {
            u64 mods[64];
            u32 nmods = planeModifiers(fd, planeId, scanFourcc, mods, 64);
            ObjPool* trial = ObjPool::fromMemoryRaw();

            for (auto& sb : scan) {
                if (createScanBuf(*vk, fd, mode.hdisplay, mode.vdisplay, mods, nmods, scanFormat, scanFourcc, sb)) {
                    scanCount++;

                    ScanBuf* slot = &sb;

                    pooledGuard(*trial, [this, slot] {
                        destroyScanBuf(*vk, fd, *slot);
                    });
                } else {
                    break;
                }
            }

            if (scanCount >= 2) {
                // registered last, runs first: the spare dies before the
                // scanout slots, matching the old teardown order
                pooledGuard(*trial, [this] {
                    destroyScanBuf(*vk, fd, screenshotReplacement);
                });
                link = trial;

                if (scanFourcc == DRM_FORMAT_XRGB2101010) {
                    sysO << "imway: 10-bit scanout"_sv << endL;
                }

                break;
            }

            // the failed attempt's guards roll its buffers back
            delete trial;
            scanCount = 0;

            if (scanFourcc == DRM_FORMAT_XRGB8888) {
                break;
            }

            // the plane advertises 2101010 but export/import did not pan
            // out — retry plain 8-bit before falling to the dumb path
            sysE << "imway: 10-bit scanout failed, retrying 8-bit"_sv << endL;
            scanFourcc = DRM_FORMAT_XRGB8888;
            scanFormat = kVkFormat;
        }
    }

    if (scanCount >= 2) {
        scanModCount = planeModifiers(fd, planeId, scanFourcc, scanMods, 64);
    }

    // a 10-bit framebuffer behind the default max-bpc-8 link dies at the
    // connector, and the 1/1023 dither is sub-LSB for an 8-bit link: ask
    // for a 10-bit link to match (the link bpc feedback reports what the
    // display actually negotiated)
    if (!color.hdr() && scanFourcc == DRM_FORMAT_XRGB2101010 && connMaxBpc &&
        !config.bpc) {
        maxBpcValue = maxBpc < 10 ? maxBpc : 10;
        color.bpc = (u32)maxBpcValue;
        sysO << "imway: requesting "_sv << maxBpcValue
             << " bpc link for the 10-bit framebuffer"_sv << endL;
    }

    if (scanCount > 0) {
        sysO << "imway: scanout swapchain: "_sv << scanCount << " images"_sv << endL;
    } else {
        sysO << "imway: dumb-buffer path (no zero-copy scanout)"_sv << endL;

        for (auto& b : bufs) {
            createDumb(b, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888);
        }
    }

    if (color.hdr() && !(scanCount > 0 && scanFourcc == DRM_FORMAT_XRGB2101010)) {
        Errno(ENOTSUP).raise("HDR requires 10-bit scanout"_sv);
    }

    initBacklight();
    if (color.hdr() && hasBrightness()) {
        // absolute PQ luminance assumes the panel sits at its calibration
        // point; actually put it there instead of hoping
        setBrightness(1.f);
        sysO << "imway: HDR pins hardware brightness to full; brightness keys adjust SDR white"_sv << endL;
    }
    sysO << "imway: kms output: "_sv << mode.hdisplay << "x"_sv << mode.vdisplay << "@"_sv << mode.vrefresh << ", connector "_sv << connectorId << ", crtc "_sv << crtcId << ", plane "_sv << planeId << endL;
}

KmsOutput::~KmsOutput() noexcept {
    if (screenshotState == 1) {
        c->offload->join();
    }

    if (ev_is_active(screenshotIo)) {
        ev_io_stop(loop, screenshotIo);
    }

    releaseDirectUse(queuedDirect, queuedDirectFrame);
    releaseDirectUse(currentDirect, currentDirectFrame);

    while (!directFbs.empty()) {
        // Qualified deliberately: virtual dispatch is unavailable during
        // destruction and this is exactly the KMS-owned cleanup path.
        KmsOutput::dropScanoutFb(directFbs.back().buf);
    }

    if (modesetDone) {
        // hand the display back clean: neutral colorspace, no luts
        drmModeAtomicReq* req = drmModeAtomicAlloc();

        if (connColorspace) {
            drmModeAtomicAddProperty(req, connectorId, connColorspace, colorspaceDefault);
        }

        if (connHdrMeta) {
            drmModeAtomicAddProperty(req, connectorId, connHdrMeta, 0);
        }

        if (crtcDegammaProp) {
            drmModeAtomicAddProperty(req, crtcId, crtcDegammaProp, 0);
        }

        if (crtcCtmProp) {
            drmModeAtomicAddProperty(req, crtcId, crtcCtmProp, 0);
        }

        if (crtcGammaProp) {
            drmModeAtomicAddProperty(req, crtcId, crtcGammaProp, 0);
        }

        drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
        drmModeAtomicFree(req);
    }

    restoreVt();

    // the link arena unwinds the scanout slots and the screenshot spare
    delete link;

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

    if (hdrMetaBlob) {
        drmModeDestroyPropertyBlob(fd, hdrMetaBlob);
    }

    if (pendingHdrMetaBlob) {
        drmModeDestroyPropertyBlob(fd, pendingHdrMetaBlob);
    }
}

int KmsOutput::width() const {
    return mode.hdisplay;
}

int KmsOutput::height() const {
    return mode.vdisplay;
}

double KmsOutput::refresh() const {
    // vrefresh is a rounded integer (59 for 59.94); derive the real rate from
    // the pixel clock so presentation-time feedback doesn't lie by ~1.6%
    if (mode.clock > 0 && mode.htotal > 0 && mode.vtotal > 0) {
        return (double)mode.clock * 1000.0 / ((double)mode.htotal * mode.vtotal);
    }

    return mode.vrefresh > 0 ? mode.vrefresh : 60.0;
}

StringView KmsOutput::outputName() const { return sv(connectorLabel); }
StringView KmsOutput::make() const { return "DRM"_sv; }
StringView KmsOutput::model() const { return sv(connectorLabel); }
int KmsOutput::physicalWidthMm() const { return physicalWmm; }
int KmsOutput::physicalHeightMm() const { return physicalHmm; }

void KmsOutput::pickPipe(StringView connector, StringView modeStr) {
    drmModeRes* res = drmModeGetResources(fd);

    STD_VERIFY(res);

    drmModeConnector* conn = nullptr;

    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector* c = drmModeGetConnector(fd, res->connectors[i]);

        if (!c) {
            continue;
        }

        bool ok = c->connection == DRM_MODE_CONNECTED && c->count_modes > 0;

        if (ok && !connector.empty()) {
            auto& name = sb();

            connectorName(c, name);
            ok = sv(name) == connector;
        }

        if (ok) {
            conn = c;
        } else {
            drmModeFreeConnector(c);
        }
    }

    if (!conn && !connector.empty()) {
        sysE << "imway: connector "_sv << connector << " not found or not connected (see imway --list)"_sv << endL;
    }

    STD_VERIFY(conn);

    connectorId = conn->connector_id;
    connectorName(conn, connectorLabel);
    physicalWmm = conn->mmWidth;
    physicalHmm = conn->mmHeight;

    if (!modeStr.empty()) {
        ModeSpec want{};

        STD_VERIFY(want.parse(modeStr));

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

    // possible_crtcs is a 32-bit mask.  A stale encoder/crtc association
    // must not turn into an undefined negative or oversized shift below.
    STD_VERIFY(crtcIndex >= 0 && crtcIndex < 32);

    drmModeFreeResources(res);

    drmModePlaneRes* planes = drmModeGetPlaneResources(fd);

    STD_VERIFY(planes);

    for (u32 i = 0; i < planes->count_planes && !(planeId && cursorPlaneId); i++) {
        drmModePlane* p = drmModeGetPlane(fd, planes->planes[i]);

        if (!p) {
            continue;
        }

        if (p->possible_crtcs & (1u << crtcIndex)) {
            u32 typeProp = getPropId(fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type");
            drmModeObjectProperties* pp = drmModeObjectGetProperties(fd, p->plane_id, DRM_MODE_OBJECT_PLANE);

            for (u32 j = 0; pp && j < pp->count_props; j++) {
                if (pp->props[j] == typeProp) {
                    if (pp->prop_values[j] == DRM_PLANE_TYPE_PRIMARY && !planeId) {
                        planeId = p->plane_id;
                    } else if (pp->prop_values[j] == DRM_PLANE_TYPE_CURSOR && !cursorPlaneId) {
                        cursorPlaneId = p->plane_id;
                    }
                }
            }

            if (pp) {
                drmModeFreeObjectProperties(pp);
            }
        }

        drmModeFreePlane(p);
    }

    drmModeFreePlaneResources(planes);
    STD_VERIFY(planeId);
}

// Vulkan hands KMS an already encoded BT.2020/PQ XR30 image. The connector
// properties describe that signal; the CRTC color pipeline stays passthrough.
bool KmsOutput::setupHdr() {
    if (!getEnumProp(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR, "Colorspace", "BT2020_RGB", &connColorspace, &colorspaceBt2020)) {
        sysE << "imway: hdr: connector has no Colorspace/BT2020_RGB"_sv << endL;

        return false;
    }

    if (!connHdrMeta) {
        sysE << "imway: hdr: connector has no HDR_OUTPUT_METADATA"_sv << endL;

        return false;
    }

    u64 mods[64];

    if (planeModifiers(fd, planeId, DRM_FORMAT_XRGB2101010, mods, 64) == 0) {
        sysE << "imway: hdr: primary plane has no XRGB2101010"_sv << endL;

        return false;
    }

    return createHdrMetadataBlob(metadata, hdrMetaBlob);
}

bool KmsOutput::createHdrMetadataBlob(const HdrOutputMetadata& value, u32& blob) {
    if (!value.hdr) {
        return false;
    }

    hdr_output_metadata meta{};

    meta.metadata_type = 0;
    meta.hdmi_metadata_type1.metadata_type = 0;
    meta.hdmi_metadata_type1.eotf = 2; // SMPTE ST 2084 (PQ)
    // CTA metadata uses chromaticity units of 0.00002 and minimum
    // luminance units of 0.0001 nit.
    const Chromaticities& p = value.primaries;
    auto chroma = [](i32 v) { return (u16)((v + 10) / 20); };

    meta.hdmi_metadata_type1.display_primaries[0] = {chroma(p.rx), chroma(p.ry)};
    meta.hdmi_metadata_type1.display_primaries[1] = {chroma(p.gx), chroma(p.gy)};
    meta.hdmi_metadata_type1.display_primaries[2] = {chroma(p.bx), chroma(p.by)};
    meta.hdmi_metadata_type1.white_point = {chroma(p.wx), chroma(p.wy)};
    meta.hdmi_metadata_type1.max_display_mastering_luminance =
        (u16)lround(fmin(value.maxNits, 65535.0));
    meta.hdmi_metadata_type1.min_display_mastering_luminance =
        (u16)lround(fmin(value.minNits * 10000.0, 65535.0));
    meta.hdmi_metadata_type1.max_cll = (u16)(value.maxCll > 65535 ? 65535 : value.maxCll);
    meta.hdmi_metadata_type1.max_fall = (u16)(value.maxFall > 65535 ? 65535 : value.maxFall);

    blob = 0;
    drmModeCreatePropertyBlob(fd, &meta, sizeof(meta), &blob);

    return blob != 0;
}

void KmsOutput::applyPendingHdrMetadata() {
    if (!hdrMetaDirty) {
        return;
    }

    if (hdrMetaBlob) {
        drmModeDestroyPropertyBlob(fd, hdrMetaBlob);
    }

    hdrMetaBlob = pendingHdrMetaBlob;
    pendingHdrMetaBlob = 0;
    hdrMetaDirty = false;
}

const OutputColorState& KmsOutput::colorState() const {
    return color;
}

void KmsOutput::setSdrWhite(double nits) {
    if (!color.hdr() || nits <= 0 || nits == color.sdrWhiteNits) {
        return;
    }

    color.setSdrWhite(nits);
    c->scene->needsFrame = true;
}

const HdrOutputMetadata& KmsOutput::hdrMetadata() const {
    return metadata;
}

void KmsOutput::setHdrMetadata(const HdrOutputMetadata& value) {
    if (value == metadata) {
        return;
    }

    if (!color.hdr() || !value.hdr || !hdrMetaBlob) {
        metadata = value;

        return;
    }

    u32 blob = 0;

    if (!createHdrMetadataBlob(value, blob)) {
        sysE << "imway: cannot create updated HDR metadata blob"_sv << endL;

        return;
    }

    if (pendingHdrMetaBlob) {
        drmModeDestroyPropertyBlob(fd, pendingHdrMetaBlob);
    }

    metadata = value;
    pendingHdrMetaBlob = blob;
    hdrMetaDirty = true;
}

void KmsOutput::setColorTemp(double kelvin) {
    double k = kelvin <= 0 || kelvin >= 6500 ? 0 : kelvin;

    if (k == tempK) {
        return;
    }

    tempK = k;
    c->scene->needsFrame = true;
}

double KmsOutput::colorTemp() const {
    return tempK;
}

bool KmsOutput::lastFlip(u64& nsec, u32& seq) const {
    if (!flipNs) {
        return false;
    }

    nsec = flipNs;
    seq = flipSeq;

    return true;
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

int KmsOutput::tryCommit(u32 fbId, bool doModeset, bool withCursor, int inFenceFd,
                         bool testOnly) {
    drmModeAtomicReq* req = drmModeAtomicAlloc();

    STD_VERIFY(req);

    if (doModeset) {
        drmModeAtomicAddProperty(req, connectorId, connCrtcId, crtcId);
        drmModeAtomicAddProperty(req, crtcId, crtcModeId, modeBlob);
        drmModeAtomicAddProperty(req, crtcId, crtcActive, 1);

        if (connMaxBpc) {
            drmModeAtomicAddProperty(req, connectorId, connMaxBpc, maxBpcValue);
        }

        if (connRange) {
            drmModeAtomicAddProperty(req, connectorId, connRange, rangeValue);
        }

        if (color.hdr()) {
            drmModeAtomicAddProperty(req, connectorId, connColorspace, colorspaceBt2020);
            drmModeAtomicAddProperty(req, connectorId, connHdrMeta,
                                     hdrMetaDirty ? pendingHdrMetaBlob : hdrMetaBlob);
            if (crtcDegammaProp) drmModeAtomicAddProperty(req, crtcId, crtcDegammaProp, 0);
            if (crtcCtmProp) drmModeAtomicAddProperty(req, crtcId, crtcCtmProp, 0);
            if (crtcGammaProp) drmModeAtomicAddProperty(req, crtcId, crtcGammaProp, 0);
        } else {
            // scrub color state left over by a previous session: connector
            // and crtc props are not reset by anyone on restart
            if (connColorspace) {
                drmModeAtomicAddProperty(req, connectorId, connColorspace, colorspaceDefault);
            }

            if (connHdrMeta) {
                drmModeAtomicAddProperty(req, connectorId, connHdrMeta, 0);
            }

            if (crtcDegammaProp) {
                drmModeAtomicAddProperty(req, crtcId, crtcDegammaProp, 0);
            }

            if (crtcCtmProp) {
                drmModeAtomicAddProperty(req, crtcId, crtcCtmProp, 0);
            }

            if (crtcGammaProp) drmModeAtomicAddProperty(req, crtcId, crtcGammaProp, 0);
        }
    }

    if (!doModeset && color.hdr() && hdrMetaDirty) {
        drmModeAtomicAddProperty(req, connectorId, connHdrMeta, pendingHdrMetaBlob);
    }

    drmModeAtomicAddProperty(req, planeId, plFbId, fbId);
    drmModeAtomicAddProperty(req, planeId, plCrtcId, crtcId);

    if (plInFenceFd && inFenceFd >= 0) {
        drmModeAtomicAddProperty(req, planeId, plInFenceFd, (u64)inFenceFd);
    }

    drmModeAtomicAddProperty(req, planeId, plSrcX, 0);
    drmModeAtomicAddProperty(req, planeId, plSrcY, 0);
    drmModeAtomicAddProperty(req, planeId, plSrcW, (u64)mode.hdisplay << 16);
    drmModeAtomicAddProperty(req, planeId, plSrcH, (u64)mode.vdisplay << 16);
    drmModeAtomicAddProperty(req, planeId, plCrtcX, 0);
    drmModeAtomicAddProperty(req, planeId, plCrtcY, 0);
    drmModeAtomicAddProperty(req, planeId, plCrtcW, mode.hdisplay);
    drmModeAtomicAddProperty(req, planeId, plCrtcH, mode.vdisplay);

    if (cursorPlaneId && (cursorEnabled || doModeset)) {
        if (withCursor && cursorEnabled) {
            addCursorProps(req);
        } else {
            // shut the plane off explicitly, otherwise the kernel keeps
            // scanning out the last cursor state — a frozen cursor on screen
            drmModeAtomicAddProperty(req, cursorPlaneId, cuFbId, 0);
            drmModeAtomicAddProperty(req, cursorPlaneId, cuCrtcId, 0);
        }
    }

    u32 flags = testOnly ? DRM_MODE_ATOMIC_TEST_ONLY :
                           DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;

    if (doModeset) {
        flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
    }

    // async flip requested by wp-tearing-control on the direct-scanout buffer
    if (!testOnly && !doModeset && tearingAllowed && (queuedDirect || currentDirect)) {
        flags |= DRM_MODE_PAGE_FLIP_ASYNC;
    }

    int ret = drmModeAtomicCommit(fd, req, flags, testOnly ? nullptr : this);

    drmModeAtomicFree(req);

    // drmModeAtomicCommit returns -errno itself; reading errno here would
    // report success on libdrm's early exits that never touch errno
    return ret == 0 ? 0 : -ret;
}

bool KmsOutput::commit(u32 fbId, bool doModeset, int inFenceFd) {
    bool withCursor = true;

    if (doModeset) {
        int testErr = tryCommit(fbId, true, true, inFenceFd, true);

        if (testErr != 0 && cursorPlaneId && cursorEnabled) {
            testErr = tryCommit(fbId, true, false, inFenceFd, true);
            withCursor = false;
        }

        if (testErr != 0 && color.hdr()) {
            // the connector rejected the HDR color/link configuration:
            // degrade to SDR on the same framebuffer instead of never
            // lighting up
            sysE << "imway: HDR modeset rejected (errno "_sv << testErr
                 << "), falling back to SDR"_sv << endL;

            OutputConfiguration sdrConfig = config;

            sdrConfig.hdrSdrWhiteNits = 0;
            sdrConfig.displayMinNits = 0;
            sdrConfig.displayPeakNits = 0;
            sdrConfig.displayMaxFallNits = 0;
            color = outputColorState(sdrConfig, displayCapabilities);
            withCursor = true;
            testErr = tryCommit(fbId, true, true, inFenceFd, true);

            if (testErr != 0 && cursorPlaneId && cursorEnabled) {
                testErr = tryCommit(fbId, true, false, inFenceFd, true);
                withCursor = false;
            }
        }

        if (testErr != 0) {
            sysE << "imway: atomic test modeset rejected color/link configuration, errno "_sv
                 << testErr << endL;
            return false;
        }

        if (!withCursor) {
            sysE << "imway: cursor plane rejected by atomic test, software cursor"_sv << endL;
            cursorPlaneId = 0;
            curW = curH = 0;
            cursorEnabled = false;
        }
    }

    int err = tryCommit(fbId, doModeset, withCursor, inFenceFd);

    if (err == EBUSY) {
        return false;
    }

    // bisect: some driver/mode combinations reject the cursor plane in the
    // same commit — retry without it and fall back to the software cursor
    if (err != 0 && cursorPlaneId && cursorEnabled) {
        int errNoCursor = tryCommit(fbId, doModeset, false, inFenceFd);

        if (errNoCursor == 0) {
            sysE << "imway: cursor plane rejected by this mode (errno "_sv << err << "), software cursor"_sv << endL;
            cursorPlaneId = 0;
            curW = curH = 0;
            cursorEnabled = false;

            applyPendingHdrMetadata();
            flipPending = true;

            return true;
        }

        err = errNoCursor;
    }

    if (err != 0) {
        sysE << "imway: kms atomic commit failed, errno "_sv << err << (doModeset ? " (modeset)"_sv : ""_sv) << endL;

        return false;
    }

    applyPendingHdrMetadata();
    flipPending = true;

    return true;
}

void KmsOutput::updateSignalFeedback() {
    if (!modesetDone || signalFeedbackLogged) {
        return;
    }

    signalFeedbackLogged = true;

    if (connLinkBpc) {
        u64 linkBpc = getPropValue(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR,
                                   "link bpc", color.bpc);

        if (linkBpc > 0) {
            color.bpc = (u32)linkBpc;

            if (color.hdr() && linkBpc < 10) {
                sysE << "imway: HDR link degraded to "_sv << linkBpc
                     << " bpc; falling back to SDR"_sv << endL;
                OutputConfiguration sdrConfig = config;

                sdrConfig.hdrSdrWhiteNits = 0;
                sdrConfig.displayMinNits = 0;
                sdrConfig.displayPeakNits = 0;
                sdrConfig.displayMaxFallNits = 0;
                color = outputColorState(sdrConfig, displayCapabilities);
                color.bpc = (u32)linkBpc;
                modesetDone = false;
                signalFeedbackLogged = false;
                c->scene->needsFrame = true;
                return;
            }
        }
    } else if (color.hdr()) {
        sysE << "imway: link bpc feedback unavailable; actual HDR link depth is unverified"_sv << endL;
    }

    if (rangeFullValue || rangeLimitedValue) {
        u64 value = getPropValue(fd, connectorId, DRM_MODE_OBJECT_CONNECTOR,
                                 "Broadcast RGB", rangeValue);

        if (value == rangeFullValue) {
            color.range = OutputRange::full;
        } else if (value == rangeLimitedValue) {
            color.range = OutputRange::limited;
        }
    }
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

int KmsOutput::cursorCapW() const {
    return color.hdr() ? 0 : curW;
}

int KmsOutput::cursorCapH() const {
    return color.hdr() ? 0 : curH;
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

void KmsOutput::initBacklight() {
    drmModeConnector* conn = drmModeGetConnector(fd, connectorId);

    if (!conn) {
        return;
    }

    bool internal = conn->connector_type == DRM_MODE_CONNECTOR_eDP || conn->connector_type == DRM_MODE_CONNECTOR_LVDS || conn->connector_type == DRM_MODE_CONNECTOR_DSI;

    StringBuilder connName;

    connectorName(conn, connName);
    drmModeFreeConnector(conn);

    if (!internal) {
        // external monitors take brightness over ddc/ci, not sysfs
        initDdc(sv(connName));

        return;
    }

    // firmware > platform > raw, per kernel docs
    int best = 0;

    try {
        listDir("/sys/class/backlight"_sv, [this, &best](const TPathInfo& e) {
            StringBuilder p;

            p << "/sys/class/backlight/"_sv << e.item << "/type"_sv;

            Buffer t;

            try {
                readFileContent(p, t);
            } catch (...) {
                return;
            }

            StringView tv = sv(t).stripCr();
            int score = tv.startsWith("firmware"_sv) ? 3 : tv.startsWith("platform"_sv) ? 2 : 1;

            if (score > best) {
                best = score;
                blPath.reset();
                blPath << "/sys/class/backlight/"_sv << e.item;
            }
        });
    } catch (...) {
        return;
    }

    if (blPath.empty()) {
        return;
    }

    auto& p = sb();

    p << sv(blPath) << "/max_brightness"_sv;

    Buffer m;

    try {
        readFileContent(p, m);
    } catch (...) {
        blPath.reset();

        return;
    }

    blMax = (long)sv(m).stou();

    if (blMax <= 0) {
        blPath.reset();

        return;
    }

    sysO << "imway: backlight "_sv << sv(blPath) << ", max "_sv << blMax << endL;
}


namespace {
    void ddcTimerCb(struct ev_loop*, ev_timer* w, int);
}

void KmsOutput::initDdc(StringView connName) {
    // the connector's i2c bus: /sys/class/drm/<card>-<conn>/ddc/i2c-dev/i2c-N
    StringBuilder busDev;

    try {
        listDir("/sys/class/drm"_sv, [this, connName, &busDev](const TPathInfo& e) {
            // exact card<N>-<conn> match: endsWith would let "DP-1" hit
            // "eDP-1" or the other GPU's connector and poke a foreign monitor
            StringView item = e.item;
            size_t digits = 4;

            while (digits < item.length() && item[digits] >= '0' && item[digits] <= '9') {
                digits++;
            }

            bool exact = item.startsWith("card"_sv) && digits > 4 && digits < item.length() &&
                         item[digits] == '-' && StringView(item.begin() + digits + 1, item.end()) == connName;

            if (!busDev.empty() || !exact) {
                return;
            }

            StringBuilder dir;

            dir << "/sys/class/drm/"_sv << e.item << "/ddc/i2c-dev"_sv;

            try {
                listDir(sv(dir), [&busDev](const TPathInfo& b) {
                    if (busDev.empty() && b.item.startsWith("i2c-"_sv)) {
                        busDev << "/dev/"_sv << b.item;
                    }
                });
            } catch (...) {
            }
        });
    } catch (...) {
        return;
    }

    if (busDev.empty()) {
        return;
    }

    ddcFd = open(Buffer(sv(busDev)).cStr(), O_RDWR | O_CLOEXEC);

    if (ddcFd < 0) {
        return;
    }

    if (ioctl(ddcFd, I2C_SLAVE, 0x37) < 0) {
        close(ddcFd);
        ddcFd = -1;

        return;
    }

    int cur = 0, max = 0;

    // probe: no reply means the monitor has ddc/ci disabled in its own menu
    if (!ddcGet(0x10, cur, max) || max <= 0) {
        close(ddcFd);
        ddcFd = -1;

        return;
    }

    ddcMax = max;
    ddcCur = cur;
    pooledFD(*pool, ddcFd);
    ddcTimer = createEvTimer(*pool, loop);
    sysO << "imway: ddc/ci brightness on "_sv << sv(busDev) << ", max "_sv << ddcMax << endL;
}

// DDC/CI Get VCP: write the request, wait, read the 11-byte reply
bool KmsOutput::ddcGet(u8 vcp, int& cur, int& max) {
    u8 req[5] = {0x51, 0x82, 0x01, vcp, 0};

    for (int i = 0; i < 4; i++) {
        req[4] ^= req[i];
    }

    req[4] ^= 0x6E;

    if (write(ddcFd, req, 5) != 5) {
        return false;
    }

    usleep(50000);

    u8 rep[11] = {};

    if (read(ddcFd, rep, 11) != 11) {
        return false;
    }

    // rep: xx 88 02 result vcp type maxH maxL curH curL cs
    if (rep[2] != 0x02 || rep[4] != vcp) {
        return false;
    }

    max = (rep[6] << 8) | rep[7];
    cur = (rep[8] << 8) | rep[9];

    return true;
}

void KmsOutput::ddcSet(u8 vcp, int val) {
    u8 msg[7] = {0x51, 0x84, 0x03, vcp, (u8)(val >> 8), (u8)(val & 0xff), 0};

    for (int i = 0; i < 6; i++) {
        msg[6] ^= msg[i];
    }

    msg[6] ^= 0x6E;
    (void)!write(ddcFd, msg, 7);
}

namespace {
    void ddcTimerCb(struct ev_loop*, ev_timer* w, int) {
        auto* o = (KmsOutput*)w->data;

        o->ddcTimerOn = false;

        if (o->ddcPending >= 0) {
            o->ddcSet(0x10, o->ddcPending);
            o->ddcPending = -1;
        }
    }
}

bool KmsOutput::hasBrightness() const {
    return !blPath.empty() || ddcFd >= 0;
}

float KmsOutput::brightness() const {
    if (ddcFd >= 0) {
        return ddcMax ? (float)ddcCur / (float)ddcMax : 0.f;
    }

    if (blPath.empty()) {
        return 0.f;
    }

    auto& p = sb();

    p << sv(blPath) << "/brightness"_sv;

    Buffer b;

    try {
        readFileContent(p, b);
    } catch (...) {
        return 0.f;
    }

    return (float)sv(b).stou() / (float)blMax;
}

void KmsOutput::setBrightness(float v) {
    v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;

    if (ddcFd >= 0) {
        // cache for the ui right away, coalesce the actual i2c write
        ddcCur = (int)lroundf(v * (float)ddcMax);
        ddcPending = ddcCur;

        if (!ddcTimerOn) {
            ddcTimerOn = true;
            ev_timer_init(ddcTimer, ddcTimerCb, 0.06, 0.);
            ddcTimer->data = this;
            ev_timer_start(loop, ddcTimer);
        }

        return;
    }

    if (blPath.empty()) {
        return;
    }

    // floor at one raw step: zero on an edp panel means a black screen and
    // a lost user
    long raw = lroundf(v * (float)blMax);

    raw = raw < 1 ? 1 : raw > blMax ? blMax : raw;

    auto& p = sb();

    p << sv(blPath) << "/brightness"_sv;

    ScopedFD f(open(p.cStr(), O_WRONLY | O_CLOEXEC));

    if (f.get() < 0) {
        return;
    }

    auto& val = sb();

    val << raw;
    FDRegular(f).write(val.data(), val.used());
}

void KmsOutput::setPowerSave(bool on) {
    if (!modesetDone || !sessionActive || on == powered) {
        return;
    }

    powered = on;

    if (on) {
        drainPendingFlip();

        u32 lastFb = scanCount > 0 ? scan[scanNext ^ 1].fbId : bufs[nextBuf ^ 1].fbId;

        if (commit(lastFb, true)) {
            queuedScan = scanCount > 0 ? scanNext ^ 1 : -1;
            queuedDirect = nullptr;
            queuedDirectFrame = nullptr;
            sysO << "imway: display back on"_sv << endL;
        }
    } else {
        drainPendingFlip();

        // blocking commit, just drops ACTIVE; planes keep their state
        drmModeAtomicReq* req = drmModeAtomicAlloc();

        drmModeAtomicAddProperty(req, crtcId, crtcActive, 0);
        drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
        drmModeAtomicFree(req);
        releaseDirectUse(queuedDirect, queuedDirectFrame);
        sysO << "imway: display off (idle)"_sv << endL;
    }
}

void KmsOutput::setupVt() {
    // the session's own vt, not a hardcoded tty1: under seatd/logind the
    // compositor often sits on tty2+ and K_OFF/KD_GRAPHICS on a foreign
    // console would blank someone else's terminal
    int vt = 0;
    const char* probes[] = {"/dev/tty", "/dev/tty0"};

    for (const char* probe : probes) {
        int fd = open(probe, O_RDWR | O_CLOEXEC);

        if (fd < 0) {
            continue;
        }

        vt_stat st{};

        if (ioctl(fd, VT_GETSTATE, &st) == 0) {
            vt = st.v_active;
        }

        close(fd);

        if (vt > 0) {
            break;
        }
    }

    if (vt <= 0) {
        sysE << "imway: cannot find own vt, input will leak to console"_sv << endL;

        return;
    }

    auto& p = sb();

    p << "/dev/tty"_sv << vt;
    ttyFd = open(p.cStr(), O_RDWR | O_CLOEXEC);

    if (ttyFd < 0) {
        sysE << "imway: "_sv << sv(p) << " unavailable, input will leak to console"_sv << endL;

        return;
    }

    if (ioctl(ttyFd, KDGKBMODE, &oldKbMode) != 0) {
        oldKbMode = -1;
    }

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

bool KmsOutput::ready() const {
    return started && !flipPending && sessionActive && connectorConnected && powered;
}

bool KmsOutput::vsynced() const {
    return true;
}

int KmsOutput::scanoutCount() const {
    return scanCount;
}

ScanoutBuffer* KmsOutput::scanoutBuffer(int i) {
    return &scan[i].pub;
}

int KmsOutput::acquire() {
    return scanNext;
}

bool KmsOutput::supportsRenderFence() const {
    return plInFenceFd != 0;
}

bool KmsOutput::presentImage(int i, int renderFenceFd) {
    if (!started || flipPending || !sessionActive) {
        return false;
    }

    if (commit(scan[i].fbId, !modesetDone, renderFenceFd)) {
        modesetDone = true;
        scanNext = i ^ 1;
        queuedScan = i;
        queuedDirect = nullptr;
        queuedDirectFrame = nullptr;

        return true;
    }

    return false;
}

bool KmsOutput::prepareScreenshot(Listener& readyListener) {
    if (scanCount < 2 || screenshotState != 0 || !scanModCount) {
        return false;
    }

    screenshotReady = &readyListener;
    screenshotState = 1;
    stdAtomicStore(&screenshotResult, 0, MemoryOrder::Relaxed);
    c->offload->submit([this] {
        bool ok = createScanBuf(*vk, fd, mode.hdisplay, mode.vdisplay,
                                scanMods, scanModCount, scanFormat,
                                scanFourcc, screenshotReplacement);

        stdAtomicStore(&screenshotResult, ok ? 1 : -1, MemoryOrder::Release);
        screenshotDone.signal();
    });

    return true;
}

void KmsOutput::screenshotPrepared() {
    screenshotDone.drain();

    int result = stdAtomicFetch(&screenshotResult, MemoryOrder::Acquire);
    Listener* listener = screenshotReady;

    screenshotReady = nullptr;
    screenshotState = result > 0 ? 2 : 0;

    if (listener) {
        listener->onListen(result > 0 ? this : nullptr);
    }
}

bool KmsOutput::takeScreenshot(int i, SharedScanout& image) {
    if (screenshotState != 2 || i < 0 || i >= scanCount) {
        return false;
    }

    ScanBuf& sb = scan[i];
    auto getMemoryFd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(
        vk->device, "vkGetMemoryFdKHR");
    VkMemoryGetFdInfoKHR info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};

    info.memory = sb.memory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    int dmaFd = -1;

    if (!getMemoryFd || getMemoryFd(vk->device, &info, &dmaFd) != VK_SUCCESS ||
        dmaFd < 0) {
        destroyScanBuf(*vk, fd, screenshotReplacement);
        screenshotState = 0;

        return false;
    }

    image.fd = dmaFd;
    image.width = (u32)mode.hdisplay;
    image.height = (u32)mode.vdisplay;
    image.format = (u32)sb.pub.format;
    image.offset = sb.offset;
    image.stride = sb.stride;
    image.modifier = sb.modifier;
    image.allocationSize = sb.allocationSize;
    image.renderDevice = vk->renderDev;
    image.color = color;

    screenshotIndex = i;
    screenshotWasPresented = false;
    screenshotState = 3;

    return true;
}

bool KmsOutput::screenshotPending() const {
    return screenshotState != 0;
}

void KmsOutput::retireScreenshot() {
    if (screenshotState != 3) {
        return;
    }

    if (currentScan == screenshotIndex) {
        screenshotWasPresented = true;

        return;
    }

    if (!screenshotWasPresented) {
        return;
    }

    ScanBuf old = scan[screenshotIndex];

    scan[screenshotIndex] = screenshotReplacement;
    screenshotReplacement = ScanBuf{};
    destroyScanBuf(*vk, fd, old);
    screenshotIndex = -1;
    screenshotWasPresented = false;
    screenshotState = 0;
}

void KmsOutput::sessionDisabled() {
    sessionActive = false;
    sysO << "imway: session disabled (vt switch away)"_sv << endL;
}

void KmsOutput::sessionEnabled() {
    bool comeback = !sessionActive;

    sessionActive = true;
    drainPendingFlip();
    releaseDirectUse(queuedDirect, queuedDirectFrame);

    if (!comeback || !modesetDone) {
        return;
    }

    u32 lastFb = scanCount > 0 ? scan[scanNext ^ 1].fbId : bufs[nextBuf ^ 1].fbId;

    if (commit(lastFb, true)) {
        queuedScan = scanCount > 0 ? scanNext ^ 1 : -1;
        queuedDirect = nullptr;
        queuedDirectFrame = nullptr;
        sysO << "imway: session enabled, remodeset"_sv << endL;
    }
}

bool KmsOutput::presentNeedsPixels() const {
    return scanCount == 0;
}

void KmsOutput::gemHandleRef(u32 handle) {
    if (int* refs = gemHandles.find(handle); refs) {
        (*refs)++;
    } else {
        gemHandles.insert(handle, 1);
    }
}

void KmsOutput::gemHandleUnref(u32 handle) {
    int* refs = gemHandles.find(handle);

    if (refs && --*refs == 0) {
        drm_gem_close gc{};

        gc.handle = handle;
        drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &gc);
        gemHandles.erase(handle);
    }
}

// import a client dmabuf as a drm framebuffer, cached by pointer
DirectFbOwner::DirectFbOwner(KmsOutput* o, DmabufBuffer* b)
    : output(o)
    , buffer(b)
{
}

DirectFbOwner::~DirectFbOwner() noexcept {
    if (buffer) {
        output->dropScanoutFb(buffer);
    }
}

u32 KmsOutput::importDirectFb(DmabufBuffer* buf) {
    for (const DirectFb& d : directFbs) {
        if (d.buf == buf) {
            return d.fb;
        }
    }

    u32 handles[4] = {}, pitches[4] = {}, offs[4] = {};
    u64 mods[4] = {};
    u32 unique[4] = {};
    int uniqueCount = 0;

    auto closeImported = [&] {
        for (int i = 0; i < uniqueCount; i++) {
            gemHandleUnref(unique[i]);
        }
    };

    for (int i = 0; i < buf->nplanes; i++) {
        if (drmPrimeFDToHandle(fd, buf->fds[i], &handles[i]) != 0) {
            closeImported();

            return 0;
        }

        bool seen = false;

        for (int j = 0; j < uniqueCount; j++) {
            seen |= unique[j] == handles[i];
        }

        if (!seen) {
            unique[uniqueCount++] = handles[i];
            gemHandleRef(handles[i]);
        }

        pitches[i] = buf->strides[i];
        offs[i] = buf->offsets[i];
        mods[i] = buf->modifier;
    }

    u32 fbId = 0;

    if (drmModeAddFB2WithModifiers(fd, buf->width, buf->height, buf->format, handles, pitches, offs, mods, &fbId, DRM_MODE_FB_MODIFIERS) != 0) {
        closeImported();

        return 0;
    }

    DirectFb d;

    d.buf = buf;
    d.fb = fbId;
    d.nh = uniqueCount;

    for (int i = 0; i < uniqueCount; i++) {
        d.handles[i] = unique[i];
    }

    d.owner = buf->lifetime->make<DirectFbOwner>(this, buf);
    directFbs.pushBack(d);

    return fbId;
}

bool KmsOutput::directScanout(DmabufBuffer* buf, FrameResource* frame) {
    if (color.hdr() || !started || flipPending || !sessionActive || !modesetDone || !buf || !frame) {
        return false;
    }

    // only a buffer that already matches the mode goes straight to the plane
    if (buf->width != mode.hdisplay || buf->height != mode.vdisplay) {
        return false;
    }

    u32 fbId = importDirectFb(buf);

    if (!fbId) {
        return false;
    }

    if (commit(fbId, false)) {
        // our own composition swapchain is now stale; the next non-direct
        // frame re-acquires and modesets nothing, just flips back
        frameRef(frame);
        queuedDirect = buf;
        queuedDirectFrame = frame;
        queuedScan = -1;

        return true;
    }

    return false;
}

void KmsOutput::dropScanoutFb(DmabufBuffer* buf) {
    if (buf == currentDirect || (flipPending && buf == queuedDirect)) {
        return;
    }

    for (size_t i = 0; i < directFbs.length(); i++) {
        if (directFbs[i].buf != buf) {
            continue;
        }

        drmModeRmFB(fd, directFbs[i].fb);

        for (int j = 0; j < directFbs[i].nh; j++) {
            gemHandleUnref(directFbs[i].handles[j]);
        }

        if (directFbs[i].owner) {
            directFbs[i].owner->buffer = nullptr;
        }

        directFbs.mut(i) = directFbs.back();
        directFbs.popBack();

        return;
    }
}

void KmsOutput::scanoutFormatsImpl(stl::VisitorFace&& vis) {
    // the RGB formats the direct scanout policy accepts; YUV stays out
    // until the plane has a color pipeline for it
    const u32 fourccs[] = {kFourccArgb, kFourccXrgb, kFourccAr30, kFourccXr30,
                           kFourccAb30, kFourccXb30, kFourccAb4h, kFourccXb4h};

    for (u32 fourcc : fourccs) {
        u64 mods[64];
        u32 count = planeModifiers(fd, planeId, fourcc, mods, 64);

        for (u32 i = 0; i < count; i++) {
            DmabufFormat format{fourcc, mods[i]};

            vis.visit(&format);
        }
    }
}

void KmsOutput::releaseDirectUse(DmabufBuffer*& buf, FrameResource*& frame) {
    if (!buf) {
        return;
    }

    buf = nullptr;
    frameUnref(frame);
    frame = nullptr;
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
        queuedScan = -1;
        queuedDirect = nullptr;
        queuedDirectFrame = nullptr;
    }
}

Device* DeviceKms::create(Composer& c, StringView devPath) {
    return c.pool->make<KmsDevice>(c, devPath);
}

void DeviceKms::list() {
    for (int i = 0; i < 8; i++) {
        auto& p = sb();

        p << "/dev/dri/card"_sv << i;

        int fd = open(p.cStr(), O_RDWR | O_CLOEXEC | O_NONBLOCK);

        if (fd < 0) {
            continue;
        }

        bool atomic = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0;
        drmVersion* ver = drmGetVersion(fd);

        sysO << sv(p) << ": "_sv << (ver && ver->name ? ver->name : "?") << (atomic ? ", atomic"_sv : ", NO atomic (unusable)"_sv) << endL;

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

                auto& name = sb();

                connectorName(conn, name);
                sysO << "  "_sv << sv(name) << (conn->connection == DRM_MODE_CONNECTED ? ": connected"_sv : ": disconnected"_sv);

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
