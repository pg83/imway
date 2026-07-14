#include "renderer.h"

#include "frame_listener.h"
#include "input_sink.h"
#include "output.h"
#include "scene.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <ev.h>
#include <linux/input-event-codes.h>

#include <vulkan/vulkan.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>

using namespace stl;

struct SurfaceTexture {
    int w = 0, h = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingMap = nullptr;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    bool needsUpload = false;
    bool firstUse = true;
    bool external = false;
};

#define VK_CHECK(x) STD_VERIFY((x) == VK_SUCCESS)

namespace {
    constexpr VkFormat kFormat = VK_FORMAT_B8G8R8A8_UNORM;

    constexpr u32 kFourccArgb = 0x34325241;
    constexpr u32 kFourccXrgb = 0x34325258;

    void frameTimerCb(struct ev_loop*, ev_timer* w, int);

    struct RendererImpl: public Renderer, public InputSink {
        InputSink* sink() override { return this; }

        struct ev_loop* loop = nullptr;
        Scene* scene = nullptr;
        ::Output* output = nullptr;
        FrameListener* listener = nullptr;
        ev_timer frameTimer{};
        int framesLimit = 0;
        int settleFrames = 0;

        int width = 0, height = 0;

        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice phys = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        u32 queueFamily = 0;
        VkQueue queue = VK_NULL_HANDLE;

        VkImage target = VK_NULL_HANDLE;
        VkDeviceMemory targetMemory = VK_NULL_HANDLE;
        VkImageView targetView = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;

        VkBuffer readback = VK_NULL_HANDLE;
        VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
        void* readbackMap = nullptr;

        VkCommandPool cmdPool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        ObjList<SurfaceTexture>* textureAlloc = nullptr;
        Vector<SurfaceTexture*> textures;

        bool hasDmabuf = false;
        Vector<DmabufFormat> dmabufFormats_;
        PFN_vkGetMemoryFdPropertiesKHR getMemoryFdProps = nullptr;

        RendererImpl(ObjPool* pool, struct ev_loop* evLoop, Scene& scn, ::Output& out, int limit) : loop(evLoop), scene(&scn), output(&out), framesLimit(limit), textureAlloc(pool->make<ObjList<SurfaceTexture>>(pool)) {
            setup(scn.outW, scn.outH);

            ImGui::GetIO().MouseDrawCursor = scn.drawCursor;

            ev_timer_init(&frameTimer, frameTimerCb, 0., 1.0 / scn.hz);
            frameTimer.data = this;
            ev_timer_start(loop, &frameTimer);
        }

        ~RendererImpl() noexcept {
            ev_timer_stop(loop, &frameTimer);
            shutdown();
        }

        void tick();

        u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags props);
        void createImage(int w, int h, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem);
        void createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf, VkDeviceMemory& mem, void** map);
        void setup(int w, int h);
        void queryDmabufFormats();
        void shutdown() noexcept;

        void drawSurfaceTree(Surface& s, float x, float y);
        void drawSurfaceTreeOverlay(Surface& s, float x, float y);
        void markTreeUnhovered(Surface& s);
        void buildUi(Scene& scene);

        void setFrameListener(FrameListener* l) override {
            listener = l;
        }

        void motion(double x, double y) override {
            scene->needsFrame = true;
            ImGui::GetIO().AddMousePosEvent((float)x, (float)y);
        }

        void button(u32 btn, bool pressed) override {
            int imguiBtn = btn == BTN_LEFT ? 0 : btn == BTN_RIGHT ? 1 : 2;

            scene->needsFrame = true;
            ImGui::GetIO().AddMouseButtonEvent(imguiBtn, pressed);
        }

        void key(u32, bool) override {
            scene->needsFrame = true;
        }

        void scroll(double value) override {
            scene->needsFrame = true;
            ImGui::GetIO().AddMouseWheelEvent(0.f, (float)-value);
        }

        size_t dmabufFormatCount() const override {
            return dmabufFormats_.length();
        }

        DmabufFormat dmabufFormat(size_t i) const override {
            return dmabufFormats_[i];
        }

        bool importDmabuf(Surface& s);
        void uploadSurface(Surface& s);
        void destroyTexture(SurfaceTexture* tex);
        void renderFrame();
        bool screenshot(const char* path) override;
    };

    void frameTimerCb(struct ev_loop*, ev_timer* w, int) {
        ((RendererImpl*)w->data)->tick();
    }
}

u32 RendererImpl::findMemoryType(u32 typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};

    vkGetPhysicalDeviceMemoryProperties(phys, &mp);

    for (u32 i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }

    return UINT32_MAX;
}

void RendererImpl::createImage(int w, int h, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};

    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = kFormat;
    ici.extent = {(u32)w, (u32)h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(device, &ici, nullptr, &img));

    VkMemoryRequirements req{};

    vkGetImageMemoryRequirements(device, img, &req);

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &mem));
    VK_CHECK(vkBindImageMemory(device, img, mem, 0));
}

void RendererImpl::createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf, VkDeviceMemory& mem, void** map) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};

    bci.size = size;
    bci.usage = usage;
    VK_CHECK(vkCreateBuffer(device, &bci, nullptr, &buf));

    VkMemoryRequirements req{};

    vkGetBufferMemoryRequirements(device, buf, &req);

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &mem));
    VK_CHECK(vkBindBufferMemory(device, buf, mem, 0));
    VK_CHECK(vkMapMemory(device, mem, 0, VK_WHOLE_SIZE, 0, map));
}

void RendererImpl::setup(int w, int h) {
    width = w;
    height = h;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};

    app.pApplicationName = "imway";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};

    instInfo.pApplicationInfo = &app;
    VK_CHECK(vkCreateInstance(&instInfo, nullptr, &instance));

    u32 n = 0;

    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    STD_VERIFY(n > 0);

    n = 1;
    vkEnumeratePhysicalDevices(instance, &n, &phys);

    VkPhysicalDeviceProperties props{};

    vkGetPhysicalDeviceProperties(phys, &props);
    sysO << "imway: vulkan device: "_sv << (const char*)props.deviceName << endL;

    u32 qn = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, nullptr);

    Vector<VkQueueFamilyProperties> qf;

    qf.zero(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf.mutData());
    queueFamily = UINT32_MAX;

    for (u32 i = 0; i < qn; i++) {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamily = i;

            break;
        }
    }

    STD_VERIFY(queueFamily != UINT32_MAX);

    Vector<const char*> devExts;
    {
        u32 en = 0;

        vkEnumerateDeviceExtensionProperties(phys, nullptr, &en, nullptr);

        Vector<VkExtensionProperties> eprops;

        eprops.zero(en);
        vkEnumerateDeviceExtensionProperties(phys, nullptr, &en, eprops.mutData());

        auto have = [&](const char* name) {
            for (const auto& e : eprops) {
                if (!strcmp(e.extensionName, name)) {
                    return true;
                }
            }

            return false;
        };

        const char* need[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME};

        hasDmabuf = true;

        for (const char* name : need) {
            if (!have(name)) {
                hasDmabuf = false;
                sysE << "imway: vulkan lacks "_sv << name << ", dmabuf disabled"_sv << endL;
            }
        }

        if (hasDmabuf) {
            for (const char* name : need) {
                devExts.pushBack(name);
            }

            if (have(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)) {
                devExts.pushBack(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
            }
        }
    }

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};

    qci.queueFamilyIndex = queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};

    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (u32)devExts.length();
    dci.ppEnabledExtensionNames = devExts.data();
    VK_CHECK(vkCreateDevice(phys, &dci, nullptr, &device));
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    if (hasDmabuf) {
        getMemoryFdProps = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR");

        if (!getMemoryFdProps) {
            hasDmabuf = false;
        }
    }

    if (hasDmabuf) {
        queryDmabufFormats();
    }

    createImage(width, height, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, target, targetMemory);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    vci.image = target;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = kFormat;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device, &vci, nullptr, &targetView));

    createHostBuffer((VkDeviceSize)width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback, readbackMemory, &readbackMap);

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
    VK_CHECK(vkCreateRenderPass(device, &rpci, nullptr, &renderPass));

    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

    fci.renderPass = renderPass;
    fci.attachmentCount = 1;
    fci.pAttachments = &targetView;
    fci.width = width;
    fci.height = height;
    fci.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &framebuffer));

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};

    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = queueFamily;
    VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &cmdPool));

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cmd));

    VkFenceCreateInfo fenci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

    VK_CHECK(vkCreateFence(device, &fenci, nullptr, &fence));

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device, &sci, nullptr, &sampler));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    ImGui_ImplVulkan_InitInfo ii{};

    ii.ApiVersion = VK_API_VERSION_1_2;
    ii.Instance = instance;
    ii.PhysicalDevice = phys;
    ii.Device = device;
    ii.QueueFamily = queueFamily;
    ii.Queue = queue;
    ii.DescriptorPoolSize = 512;
    ii.MinImageCount = 2;
    ii.ImageCount = 2;
    ii.PipelineInfoMain.RenderPass = renderPass;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    STD_VERIFY(ImGui_ImplVulkan_Init(&ii));
}

void RendererImpl::queryDmabufFormats() {
    VkDrmFormatModifierPropertiesListEXT modList{
        VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
    VkFormatProperties2 props{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

    props.pNext = &modList;
    vkGetPhysicalDeviceFormatProperties2(phys, kFormat, &props);

    Vector<VkDrmFormatModifierPropertiesEXT> mods;

    mods.zero(modList.drmFormatModifierCount);
    modList.pDrmFormatModifierProperties = mods.mutData();
    vkGetPhysicalDeviceFormatProperties2(phys, kFormat, &props);

    for (const auto& m : mods) {
        if (m.drmFormatModifierPlaneCount != 1) {
            continue;
        }

        if (!(m.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            continue;
        }

        dmabufFormats_.pushBack({kFourccArgb, m.drmFormatModifier});
        dmabufFormats_.pushBack({kFourccXrgb, m.drmFormatModifier});
    }

    sysO << "imway: dmabuf formats: "_sv << dmabufFormats_.length() << " (modifiers per fourcc: "_sv << dmabufFormats_.length() / 2 << ")"_sv << endL;
}

void RendererImpl::uploadSurface(Surface& s) {
    if (s.width <= 0 || s.height <= 0) {
        return;
    }

    SurfaceTexture* tex = s.texture;

    if (tex && (tex->w != s.width || tex->h != s.height)) {
        destroyTexture(tex);
        tex = nullptr;
        s.texture = nullptr;
    }

    if (!tex) {
        tex = textureAlloc->make();
        tex->w = s.width;
        tex->h = s.height;

        try {
            createImage(s.width, s.height, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, tex->image, tex->memory);
            createHostBuffer((VkDeviceSize)s.width * s.height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tex->staging, tex->stagingMemory, &tex->stagingMap);
        } catch (...) {
            sysE << "imway: texture allocation failed "_sv << s.width << "x"_sv << s.height << endL;
            textureAlloc->release(tex);

            return;
        }

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

        vci.image = tex->image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = kFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vci, nullptr, &tex->view);
        tex->ds = ImGui_ImplVulkan_AddTexture(sampler, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        textures.pushBack(tex);
        s.texture = tex;
    }

    memcpy(tex->stagingMap, s.pixels.data(), s.pixels.length());
    tex->needsUpload = true;
}

void RendererImpl::destroyTexture(SurfaceTexture* tex) {
    if (!tex) {
        return;
    }

    vkQueueWaitIdle(queue);

    if (tex->ds) {
        ImGui_ImplVulkan_RemoveTexture(tex->ds);
    }

    if (tex->view) {
        vkDestroyImageView(device, tex->view, nullptr);
    }

    if (tex->image) {
        vkDestroyImage(device, tex->image, nullptr);
    }

    if (tex->memory) {
        vkFreeMemory(device, tex->memory, nullptr);
    }

    if (tex->staging) {
        vkDestroyBuffer(device, tex->staging, nullptr);
    }

    if (tex->stagingMemory) {
        vkFreeMemory(device, tex->stagingMemory, nullptr);
    }

    removeOne(textures, tex);
    textureAlloc->release(tex);
}

bool RendererImpl::importDmabuf(Surface& s) {
    DmabufBuffer* b = s.dmabuf;

    if (!b || !hasDmabuf) {
        return false;
    }

    if (b->nplanes != 1) {
        return false;
    }

    destroyTexture(s.texture);
    s.texture = nullptr;

    auto* tex = textureAlloc->make();

    tex->w = b->width;
    tex->h = b->height;
    tex->external = true;

    VkSubresourceLayout plane{};

    plane.offset = b->offsets[0];
    plane.rowPitch = b->strides[0];

    VkImageDrmFormatModifierExplicitCreateInfoEXT modInfo{
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};

    modInfo.drmFormatModifier = b->modifier;
    modInfo.drmFormatModifierPlaneCount = 1;
    modInfo.pPlaneLayouts = &plane;

    VkExternalMemoryImageCreateInfo extInfo{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};

    extInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    extInfo.pNext = &modInfo;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};

    ici.pNext = &extInfo;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = kFormat;
    ici.extent = {(u32)b->width, (u32)b->height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ici, nullptr, &tex->image) != VK_SUCCESS) {
        sysE << "imway: dmabuf vkCreateImage failed"_sv << endL;
        textureAlloc->release(tex);

        return false;
    }

    int fd = dup(b->fds[0]);
    VkMemoryFdPropertiesKHR fdProps{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};

    if (getMemoryFdProps(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdProps) != VK_SUCCESS) {
        sysE << "imway: vkGetMemoryFdPropertiesKHR failed"_sv << endL;
        close(fd);
        vkDestroyImage(device, tex->image, nullptr);
        textureAlloc->release(tex);

        return false;
    }

    VkMemoryRequirements req{};

    vkGetImageMemoryRequirements(device, tex->image, &req);

    u32 typeBits = req.memoryTypeBits & fdProps.memoryTypeBits;
    u32 memType = UINT32_MAX;

    for (u32 i = 0; i < 32 && memType == UINT32_MAX; i++) {
        if (typeBits & (1u << i)) {
            memType = i;
        }
    }

    VkImportMemoryFdInfoKHR importInfo{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};

    importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    importInfo.fd = fd;

    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};

    dedicated.image = tex->image;
    dedicated.pNext = &importInfo;

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

    mai.pNext = &dedicated;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = memType;

    if (memType == UINT32_MAX || vkAllocateMemory(device, &mai, nullptr, &tex->memory) != VK_SUCCESS || vkBindImageMemory(device, tex->image, tex->memory, 0) != VK_SUCCESS) {
        sysE << "imway: dmabuf memory import failed"_sv << endL;
        close(fd);
        vkDestroyImage(device, tex->image, nullptr);

        if (tex->memory) {
            vkFreeMemory(device, tex->memory, nullptr);
        }

        textureAlloc->release(tex);

        return false;
    }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    vci.image = tex->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = kFormat;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (b->format == kFourccXrgb) {
        vci.components.a = VK_COMPONENT_SWIZZLE_ONE;
    }

    vkCreateImageView(device, &vci, nullptr, &tex->view);

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;

    VkCommandBuffer once = VK_NULL_HANDLE;

    vkAllocateCommandBuffers(device, &cbai, &once);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(once, &bi);

    VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.image = tex->image;
    toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(once, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
    vkEndCommandBuffer(once);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

    si.commandBufferCount = 1;
    si.pCommandBuffers = &once;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, cmdPool, 1, &once);

    tex->ds = ImGui_ImplVulkan_AddTexture(sampler, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    tex->firstUse = false;
    textures.pushBack(tex);
    s.texture = tex;

    return true;
}

void RendererImpl::drawSurfaceTree(Surface& s, float x, float y) {
    for (Subsurface* c : s.stackBelow) {
        if (c->surface && c->surface->hasContent) {
            drawSurfaceTree(*c->surface, x + (float)c->x, y + (float)c->y);
        }
    }

    if (s.texture) {
        ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);

        if (s.vp.hasSrc && s.texture->w > 0 && s.texture->h > 0) {
            uv0 = ImVec2((float)(s.vp.sx / s.texture->w), (float)(s.vp.sy / s.texture->h));
            uv1 = ImVec2((float)((s.vp.sx + s.vp.sw) / s.texture->w), (float)((s.vp.sy + s.vp.sh) / s.texture->h));
        }

        float w = (float)s.viewW(), h = (float)s.viewH();

        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::Image((ImTextureID)(uintptr_t)s.texture->ds, ImVec2(w, h), uv0, uv1);
        s.imgX = x;
        s.imgY = y;
        s.hovered = ImGui::IsItemHovered();
    }

    for (Subsurface* c : s.stackAbove) {
        if (c->surface && c->surface->hasContent) {
            drawSurfaceTree(*c->surface, x + (float)c->x, y + (float)c->y);
        }
    }
}

void RendererImpl::drawSurfaceTreeOverlay(Surface& s, float x, float y) {
    for (Subsurface* c : s.stackBelow) {
        if (c->surface && c->surface->hasContent) {
            drawSurfaceTreeOverlay(*c->surface, x + (float)c->x, y + (float)c->y);
        }
    }

    if (s.texture) {
        ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);

        if (s.vp.hasSrc && s.texture->w > 0 && s.texture->h > 0) {
            uv0 = ImVec2((float)(s.vp.sx / s.texture->w), (float)(s.vp.sy / s.texture->h));
            uv1 = ImVec2((float)((s.vp.sx + s.vp.sw) / s.texture->w), (float)((s.vp.sy + s.vp.sh) / s.texture->h));
        }

        float w = (float)s.viewW(), h = (float)s.viewH();

        ImGui::GetForegroundDrawList()->AddImage((ImTextureID)(uintptr_t)s.texture->ds, ImVec2(x, y), ImVec2(x + w, y + h), uv0, uv1);
        s.imgX = x;
        s.imgY = y;

        ImVec2 m = ImGui::GetIO().MousePos;

        s.hovered = m.x >= x && m.y >= y && m.x < x + w && m.y < y + h;
    }

    for (Subsurface* c : s.stackAbove) {
        if (c->surface && c->surface->hasContent) {
            drawSurfaceTreeOverlay(*c->surface, x + (float)c->x, y + (float)c->y);
        }
    }
}

void RendererImpl::markTreeUnhovered(Surface& s) {
    s.hovered = false;

    for (Subsurface* c : s.stackBelow) {
        if (c->surface) {
            markTreeUnhovered(*c->surface);
        }
    }

    for (Subsurface* c : s.stackAbove) {
        if (c->surface) {
            markTreeUnhovered(*c->surface);
        }
    }
}

void RendererImpl::buildUi(Scene& scene) {
    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = (float)(1.0 / scene.hz);

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar()) {
        ImGui::TextUnformatted("imway");
        ImGui::Separator();
        ImGui::Text("clients: %zu", scene.toplevels.length());
        ImGui::Separator();
        ImGui::Text("frame %d", scene.framesDone);
        ImGui::EndMainMenuBar();
    }

    int i = 0;

    for (Toplevel* t : scene.toplevels) {
        Surface* root = t->surface;

        if (!t->mapped || !root || !root->texture) {
            if (root) {
                markTreeUnhovered(*root);
            }

            continue;
        }

        char label[320];

        snprintf(label, sizeof label, "%s###toplevel%llu", t->title, (unsigned long long)t->id);
        ImGui::SetNextWindowPos(ImVec2(40.f + 30.f * i, 60.f + 30.f * i), ImGuiCond_FirstUseEver);
        i++;

        if (!t->winSizeSet) {
            const ImGuiStyle& st = ImGui::GetStyle();

            ImGui::SetNextWindowSize(ImVec2((float)root->viewW() + st.WindowPadding.x * 2, (float)root->viewH() + st.WindowPadding.y * 2 + ImGui::GetFrameHeight()), ImGuiCond_Always);
            t->winSizeSet = true;
        }

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::Begin(label, nullptr, flags)) {
            ImVec2 avail = ImGui::GetContentRegionAvail();

            t->desiredW = (int)avail.x;
            t->desiredH = (int)avail.y;

            ImVec2 origin = ImGui::GetCursorScreenPos();

            drawSurfaceTree(*root, origin.x, origin.y);
        } else {
            markTreeUnhovered(*root);
        }

        ImGui::End();
    }

    for (Popup* p : scene.popups) {
        Surface* ps = p->surface;

        if (!p->mapped || !ps || !ps->texture || !p->parent) {
            if (ps) {
                markTreeUnhovered(*ps);
            }

            continue;
        }

        drawSurfaceTreeOverlay(*ps, p->parent->imgX + (float)p->x, p->parent->imgY + (float)p->y);
    }

    ImGui::Render();
}

void RendererImpl::renderFrame() {
    buildUi(*scene);

    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    for (SurfaceTexture* tex : textures) {
        if (!tex->needsUpload) {
            continue;
        }

        VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

        toDst.srcAccessMask = tex->firstUse ? 0 : VK_ACCESS_SHADER_READ_BIT;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toDst.oldLayout = tex->firstUse ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = tex->image;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, tex->firstUse ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

        VkBufferImageCopy region{};

        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(u32)tex->w, (u32)tex->h, 1};
        vkCmdCopyBufferToImage(cmd, tex->staging, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toRead = toDst;

        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
        tex->needsUpload = false;
        tex->firstUse = false;
    }

    VkClearValue clear{};

    clear.color = {{0.08f, 0.08f, 0.10f, 1.f}};

    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};

    rbi.renderPass = renderPass;
    rbi.framebuffer = framebuffer;
    rbi.renderArea = {{0, 0}, {(u32)width, (u32)height}};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);

    VkBufferImageCopy region{};

    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {(u32)width, (u32)height, 1};
    vkCmdCopyImageToBuffer(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);
}

bool RendererImpl::screenshot(const char* path) {
    FILE* f = fopen(path, "wb");

    if (!f) {
        return false;
    }

    fprintf(f, "P6\n%d %d\n255\n", width, height);

    auto* px = (const unsigned char*)readbackMap;
    Vector<u8> row;

    row.zero((size_t)width * 3);

    for (int y = 0; y < height; y++) {
        const unsigned char* src = px + (size_t)y * width * 4;

        for (int x = 0; x < width; x++) {
            row.mut(x * 3 + 0) = src[x * 4 + 2];
            row.mut(x * 3 + 1) = src[x * 4 + 1];
            row.mut(x * 3 + 2) = src[x * 4 + 0];
        }

        fwrite(row.data(), 1, row.length(), f);
    }

    fclose(f);

    return true;
}

void RendererImpl::shutdown() noexcept {
    if (!device) {
        return;
    }

    vkDeviceWaitIdle(device);

    while (!textures.empty()) {
        destroyTexture(textures.back());
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    vkDestroySampler(device, sampler, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);

    if (readbackMap) {
        vkUnmapMemory(device, readbackMemory);
    }

    vkDestroyBuffer(device, readback, nullptr);
    vkFreeMemory(device, readbackMemory, nullptr);
    vkDestroyImageView(device, targetView, nullptr);
    vkDestroyImage(device, target, nullptr);
    vkFreeMemory(device, targetMemory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE;
}

void RendererImpl::tick() {
    if (scene->needsFrame) {
        settleFrames = 3;
    }

    bool active = scene->needsFrame || settleFrames > 0;

    scene->needsFrame = false;

    if (active) {
        settleFrames--;

        while (!scene->orphanedTextures.empty()) {
            destroyTexture(scene->orphanedTextures.popBack());
        }

        for (Surface* s : scene->surfaces) {
            if (s->dirty && s->hasContent) {
                if (s->dmabuf) {
                    importDmabuf(*s);
                } else {
                    uploadSurface(*s);
                }

                s->dirty = false;
            }
        }

        renderFrame();
        output->present(readbackMap);

        if (listener) {
            listener->frameShown(nowMsec());
        }
    }

    scene->framesDone++;

    if (framesLimit > 0 && scene->framesDone >= framesLimit) {
        ev_break(loop, EVBREAK_ALL);
    }
}

Renderer* Renderer::create(ObjPool* pool, struct ev_loop* loop, Scene& scene, ::Output& output, int framesLimit) {
    return pool->make<RendererImpl>(pool, loop, scene, output, framesLimit);
}
