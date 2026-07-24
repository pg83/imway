#include "device_vk.h"

#include "log.h"
#include "util.h"
#include "scene.h"
#include "device.h"
#include "output.h"
#include "session.h"
#include "composer.h"
#include "renderer.h"

#include <std/sys/fd.h>
#include <std/sys/fs.h>
#include <std/ios/sys.h>
#include <std/str/view.h>
#include <std/sys/throw.h>
#include <std/dbg/verify.h>
#include <std/ios/out_fd.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/ios/fs_utils.h>
#include <std/mem/obj_pool.h>

#include <ev.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <libudev.h>
#include <xf86drm.h>
#include <linux/kd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <drm_fourcc.h>
#include <xf86drmMode.h>
#include <sys/sysmacros.h>

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

namespace {
    VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugLog(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
        *(Log*)user << "vk: "_sv << stl::StringView(data->pMessage) << endL;

        return VK_FALSE;
    }

    bool haveInstanceExt(const char* name) {
        u32 n = 0;

        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);

        stl::Vector<VkExtensionProperties> props;

        props.zero(n);
        vkEnumerateInstanceExtensionProperties(nullptr, &n, props.mutData());

        for (const auto& e : props) {
            if (stl::StringView(e.extensionName) == stl::StringView(name)) {
                return true;
            }
        }

        return false;
    }
}

void vkWaitOrDie(VkDevice device, VkFence fence, const char* what) {
    VkResult res = vkWaitForFences(device, 1, &fence, VK_TRUE, kGpuWaitNs);

    if (res != VK_SUCCESS) {
        sysE << "imway: gpu fatal in "_sv << StringView(what) << " ("_sv << (long)res << "), exiting"_sv << endL;
        exit(1);
    }
}

DeviceVk::DeviceVk(Log& l, int drmFd)
    : log(&l)
{
    this->drmFd = drmFd;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};

    app.pApplicationName = "imway";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};

    instInfo.pApplicationInfo = &app;

    // loader and validation warnings go through the log instead of stderr
    const char* debugExt = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    bool debugUtils = haveInstanceExt(debugExt);

    if (debugUtils) {
        instInfo.enabledExtensionCount = 1;
        instInfo.ppEnabledExtensionNames = &debugExt;
    }

    VK_CHECK(vkCreateInstance(&instInfo, nullptr, &this->instance));

    if (debugUtils) {
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(this->instance, "vkCreateDebugUtilsMessengerEXT");

        if (create) {
            VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};

            dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dbg.pfnUserCallback = vkDebugLog;
            dbg.pUserData = this->log;
            create(this->instance, &dbg, nullptr, &this->debugMessenger);
        }
    }

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
            *log << "imway: no vulkan device matches the drm node, render/display are split (readback path)"_sv << endL;
        }
    }

    VkPhysicalDeviceProperties props{};

    vkGetPhysicalDeviceProperties(this->phys, &props);
    *log << "imway: vulkan device: "_sv << (const char*)props.deviceName << endL;
    this->maxImageDim = props.limits.maxImageDimension2D;

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
            *log << "imway: vulkan lacks "_sv << name << ", dmabuf disabled"_sv << endL;
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
            *log << "imway: no SYNC_FD semaphores, implicit-sync bridge disabled"_sv << endL;
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

    if (this->debugMessenger) {
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(this->instance, "vkDestroyDebugUtilsMessengerEXT");

        if (destroy) {
            destroy(this->instance, this->debugMessenger, nullptr);
        }

        this->debugMessenger = VK_NULL_HANDLE;
    }

    if (this->instance) {
        vkDestroyInstance(this->instance, nullptr);
    }

    this->device = VK_NULL_HANDLE;
    this->instance = VK_NULL_HANDLE;
}

void DeviceVk::queryDmabufFormatsImpl(VisitorFace&& vis) const {
    int n = 0;

    auto modifierImportable = [&](VkFormat format, const VkDrmFormatModifierPropertiesEXT& modifier, bool mutablePlanes) {
        VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};

        external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

        VkPhysicalDeviceImageDrmFormatModifierInfoEXT drm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT};

        drm.pNext = &external;
        drm.drmFormatModifier = modifier.drmFormatModifier;
        drm.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkPhysicalDeviceImageFormatInfo2 info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};

        info.pNext = &drm;
        info.format = format;
        info.type = VK_IMAGE_TYPE_2D;
        info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        info.flags = mutablePlanes ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;

        VkExternalImageFormatProperties externalProps{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
        VkImageFormatProperties2 props{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};

        props.pNext = &externalProps;

        return vkGetPhysicalDeviceImageFormatProperties2(this->phys, &info, &props) == VK_SUCCESS && (externalProps.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
    };

    auto addFormat = [&](VkFormat vkFormat, u32 alphaFourcc, u32 opaqueFourcc, bool mutablePlanes) {
        VkDrmFormatModifierPropertiesListEXT modList{VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
        VkFormatProperties2 props{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

        props.pNext = &modList;
        vkGetPhysicalDeviceFormatProperties2(this->phys, vkFormat, &props);

        Vector<VkDrmFormatModifierPropertiesEXT> mods;

        mods.zero(modList.drmFormatModifierCount);
        modList.pDrmFormatModifierProperties = mods.mutData();
        vkGetPhysicalDeviceFormatProperties2(this->phys, vkFormat, &props);

        for (const auto& m : mods) {
            if (m.drmFormatModifierPlaneCount > (u32)kDmabufMaxPlanes || !(m.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) || !modifierImportable(vkFormat, m, mutablePlanes)) {
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

    addFormat(kVkFormat, kFourccArgb, kFourccXrgb, false);
    addFormat(VK_FORMAT_A2R10G10B10_UNORM_PACK32, kFourccAr30, kFourccXr30, false);
    addFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32, kFourccAb30, kFourccXb30, false);
    addFormat(VK_FORMAT_R16G16B16A16_SFLOAT, kFourccAb4h, kFourccXb4h, false);
    addFormat(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, kFourccNv12, 0, true);
    addFormat(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16, kFourccP010, 0, true);

    *log << "imway: dmabuf formats: "_sv << n << endL;
}
