// Проба Vulkan-окружения: перечисляет устройства и печатает, какие из нужных
// imway расширений/фич доступны. Это ответ на M0-вопросы про lavapipe в VM.
// Падает только если нет ни одного устройства; отсутствие расширений — репорт.

#include <cstdio>
#include <cstring>
#include <vector>

#include <vulkan/vulkan.h>

namespace {

const char* const kRequired[] = {
    VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
    VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME,
};

bool has_ext(const std::vector<VkExtensionProperties>& exts, const char* name) {
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

} // namespace

int main() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "imway-vulkan-probe";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance instance{};
    if (VkResult r = vkCreateInstance(&ici, nullptr, &instance); r != VK_SUCCESS) {
        std::fprintf(stderr, "vkCreateInstance: %d\n", r);
        return 1;
    }

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance, &n, devs.data());
    std::printf("physical devices: %u\n", n);

    for (auto dev : devs) {
        VkPhysicalDeviceVulkan12Features feat12{};
        feat12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 feat2{};
        feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feat2.pNext = &feat12;
        vkGetPhysicalDeviceFeatures2(dev, &feat2);

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev, &props);

        uint32_t en = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &en, nullptr);
        std::vector<VkExtensionProperties> exts(en);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &en, exts.data());

        std::printf("\n%s (api %d.%d.%d, %s)\n", props.deviceName,
                    VK_API_VERSION_MAJOR(props.apiVersion),
                    VK_API_VERSION_MINOR(props.apiVersion),
                    VK_API_VERSION_PATCH(props.apiVersion),
                    props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? "cpu" : "gpu");

        for (const char* name : kRequired)
            std::printf("  %-42s %s\n", name, has_ext(exts, name) ? "ok" : "MISSING");
        std::printf("  %-42s %s\n", "timelineSemaphore",
                    feat12.timelineSemaphore ? "ok" : "MISSING");

        if (has_ext(exts, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) {
            VkPhysicalDeviceDrmPropertiesEXT drm{};
            drm.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
            VkPhysicalDeviceProperties2 props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &drm;
            vkGetPhysicalDeviceProperties2(dev, &props2);
            std::printf("  drm node: primary=%d (%lld:%lld) render=%d (%lld:%lld)\n",
                        drm.hasPrimary, (long long)drm.primaryMajor, (long long)drm.primaryMinor,
                        drm.hasRender, (long long)drm.renderMajor, (long long)drm.renderMinor);
        }
    }

    vkDestroyInstance(instance, nullptr);
    return n > 0 ? 0 : 1;
}
