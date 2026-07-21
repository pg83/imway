#include "composer.h"
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
#include <std/ios/fs_utils.h>
#include <std/ios/out_fd.h>
#include <std/ios/sys.h>
#include <std/sys/fs.h>
#include <std/sys/fd.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>


using namespace stl;

bool ModeSpec::parse(StringView s) {
    StringView wh = s, hz;
    StringView v = s;

    v.split('@', wh, hz);

    StringView ws, hs;

    if (!wh.split('x', ws, hs) || ws.empty() || hs.empty()) {
        return false;
    }

    this->w = (int)ws.stou();
    this->h = (int)hs.stou();
    this->hz = hz.empty() ? 0 : (double)hz.stou();

    return this->w > 0 && this->h > 0;
}

DeviceVk::DeviceVk(int drmFd) {
    this->drmFd = drmFd;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};

    app.pApplicationName = "imway";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};

    instInfo.pApplicationInfo = &app;
    VK_CHECK(vkCreateInstance(&instInfo, nullptr, &this->instance));

    u32 n = 0;

    vkEnumeratePhysicalDevices(this->instance, &n, nullptr);
    STD_VERIFY(n > 0);

    Vector<VkPhysicalDevice> devs;

    devs.zero(n);
    vkEnumeratePhysicalDevices(this->instance, &n, devs.mutData());

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

    this->phys = VK_NULL_HANDLE;

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
                    this->phys = d;

                    break;
                }
            }
        }
    }

    if (this->phys == VK_NULL_HANDLE) {
        this->phys = devs[0];

        if (drmFd >= 0) {
            sysO << "imway: no vulkan device matches the drm node, render/display are split (readback path)"_sv << endL;
        }
    }

    VkPhysicalDeviceProperties props{};

    vkGetPhysicalDeviceProperties(this->phys, &props);
    sysO << "imway: vulkan device: "_sv << (const char*)props.deviceName << endL;

    if (hasExt(this->phys, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) {
        VkPhysicalDeviceDrmPropertiesEXT drm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};

        p2.pNext = &drm;
        vkGetPhysicalDeviceProperties2(this->phys, &p2);

        if (drm.hasRender) {
            this->renderDev = makedev((u32)drm.renderMajor, (u32)drm.renderMinor);
        } else if (drm.hasPrimary) {
            this->renderDev = makedev((u32)drm.primaryMajor, (u32)drm.primaryMinor);
        }
    }

    u32 qn = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(this->phys, &qn, nullptr);

    Vector<VkQueueFamilyProperties> qf;

    qf.zero(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(this->phys, &qn, qf.mutData());
    this->queueFamily = UINT32_MAX;

    for (u32 i = 0; i < qn; i++) {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            this->queueFamily = i;

            break;
        }
    }

    STD_VERIFY(this->queueFamily != UINT32_MAX);

    Vector<const char*> devExts;
    const char* need[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME};

    this->hasDmabuf = true;

    for (const char* name : need) {
        if (!hasExt(this->phys, name)) {
            this->hasDmabuf = false;
            sysE << "imway: vulkan lacks "_sv << name << ", dmabuf disabled"_sv << endL;
        }
    }

    if (this->hasDmabuf) {
        for (const char* name : need) {
            devExts.pushBack(name);
        }

        if (hasExt(this->phys, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)) {
            devExts.pushBack(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
        }

        if (hasExt(this->phys, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME)) {
            VkPhysicalDeviceExternalSemaphoreInfo semInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};

            semInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

            VkExternalSemaphoreProperties semProps{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};

            vkGetPhysicalDeviceExternalSemaphoreProperties(this->phys, &semInfo, &semProps);

            if ((semProps.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) && (semProps.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)) {
                devExts.pushBack(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
                this->hasSyncFd = true;
            }
        }

        if (!this->hasSyncFd) {
            sysO << "imway: no SYNC_FD semaphores, implicit-sync bridge disabled"_sv << endL;
        }
    }

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};

    qci.queueFamilyIndex = this->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};

    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (u32)devExts.length();
    dci.ppEnabledExtensionNames = devExts.data();
    VK_CHECK(vkCreateDevice(this->phys, &dci, nullptr, &this->device));
    vkGetDeviceQueue(this->device, this->queueFamily, 0, &this->queue);

    if (this->hasDmabuf) {
        this->getMemoryFdProps = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(this->device, "vkGetMemoryFdPropertiesKHR");

        if (!this->getMemoryFdProps) {
            this->hasDmabuf = false;
        }
    }
}

DeviceVk::~DeviceVk() noexcept {
    if (this->device) {
        vkDestroyDevice(this->device, nullptr);
    }

    if (this->instance) {
        vkDestroyInstance(this->instance, nullptr);
    }

    this->device = VK_NULL_HANDLE;
    this->instance = VK_NULL_HANDLE;
}

void DeviceVk::queryDmabufFormatsImpl(VisitorFace&& vis) const {
    int n = 0;

    auto addFormat = [&](VkFormat vkFormat, u32 alphaFourcc, u32 opaqueFourcc) {
        VkDrmFormatModifierPropertiesListEXT modList{
            VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
        VkFormatProperties2 props{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

        props.pNext = &modList;
        vkGetPhysicalDeviceFormatProperties2(this->phys, vkFormat, &props);

        Vector<VkDrmFormatModifierPropertiesEXT> mods;

        mods.zero(modList.drmFormatModifierCount);
        modList.pDrmFormatModifierProperties = mods.mutData();
        vkGetPhysicalDeviceFormatProperties2(this->phys, vkFormat, &props);

        for (const auto& m : mods) {
            if (m.drmFormatModifierPlaneCount > (u32)kDmabufMaxPlanes ||
                !(m.drmFormatModifierTilingFeatures &
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
                continue;
            }

            DmabufFormat alpha{alphaFourcc, m.drmFormatModifier};

            vis.visit(&alpha);
            n++;

            if (opaqueFourcc) {
                DmabufFormat opaque{opaqueFourcc, m.drmFormatModifier};

                vis.visit(&opaque);
                n++;
            }
        }
    };

    addFormat(kVkFormat, kFourccArgb, kFourccXrgb);
    addFormat(VK_FORMAT_A2R10G10B10_UNORM_PACK32,
              kFourccAr30, kFourccXr30);
    addFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32,
              kFourccAb30, kFourccXb30);
    addFormat(VK_FORMAT_R16G16B16A16_SFLOAT,
              kFourccAb4h, kFourccXb4h);
    addFormat(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, kFourccNv12, 0);
    addFormat(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16,
              kFourccP010, 0);

    sysO << "imway: dmabuf formats: "_sv << n << endL;
}
