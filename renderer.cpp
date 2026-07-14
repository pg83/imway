#include "renderer.h"

#include "device_vk.h"
#include "frame_listener.h"
#include "input_sink.h"
#include "output.h"
#include "scene.h"
#include "util.h"

#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>

#include <linux/dma-buf.h>

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
#include <std/str/builder.h>
#include <std/sys/fd.h>

using namespace stl;

struct SurfaceTexture {
    int w = 0, h = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceMemory extraMemory[3] = {};
    VkImageView view = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingMap = nullptr;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    RectI uploadRect;
    bool needsUpload = false;
    bool firstUse = true;
    bool external = false;
};

#define VK_CHECK(x) STD_VERIFY((x) == VK_SUCCESS)

namespace {
    void frameTimerCb(struct ev_loop*, ev_timer* w, int);
    void prepareCb(struct ev_loop*, ev_prepare* w, int);

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

        struct DmabufCacheEntry {
            DmabufBuffer* key = nullptr;
            SurfaceTexture* tex = nullptr;
        };

        Vector<DmabufCacheEntry> dmabufCache;

        bool scanout = false;
        Vector<VkImageView> scanViews;
        Vector<VkFramebuffer> scanFbs;

        Toplevel* moving = nullptr;
        ImVec2 moveOff{};
        Toplevel* resizing = nullptr;
        u32 activeEdges = 0;
        ImVec2 resizeStartSz{}, resizeStartPos{}, resizeStartMouse{};

        const char* fontPath = nullptr;
        float uiScale = 1.f;

        // hardware cursor plane state
        bool hwCursorReady = false;
        int hwCapW = 0, hwCapH = 0;
        int hwHotX = 0, hwHotY = 0;
        bool hwVisible = false;
        int hwKind = -2;               // ImGuiMouseCursor of the uploaded image; -2 nothing, -3 client surface
        Surface* hwSurf = nullptr;
        bool hwSurfStale = false;
        Vector<u32> hwShapeCache[ImGuiMouseCursor_COUNT];
        Vector<u32> hwScratch;

        // one-off offscreen rendering of cursor shapes
        VkImage curImg = VK_NULL_HANDLE;
        VkDeviceMemory curImgMem = VK_NULL_HANDLE;
        VkImageView curView = VK_NULL_HANDLE;
        VkFramebuffer curFb = VK_NULL_HANDLE;
        VkBuffer curReadback = VK_NULL_HANDLE;
        VkDeviceMemory curReadbackMem = VK_NULL_HANDLE;
        void* curReadbackMap = nullptr;
        VkCommandBuffer curCmd = VK_NULL_HANDLE;
        VkFence curFence = VK_NULL_HANDLE;

        int drmFd = -1;
        bool hasSyncFd = false;
        VkSemaphore syncOut = VK_NULL_HANDLE;
        VkSemaphore syncWaitPool[16] = {};
        PFN_vkImportSemaphoreFdKHR importSemFd = nullptr;
        PFN_vkGetSemaphoreFdKHR getSemFd = nullptr;
        Vector<int> frameSyncFds;

        ev_prepare prep{};
        bool haveFrame = false;
        VkImage lastImage = VK_NULL_HANDLE;
        VkImageLayout lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        bool hasDmabuf = false;
        PFN_vkGetMemoryFdPropertiesKHR getMemoryFdProps = nullptr;

        RendererImpl(ObjPool* pool, struct ev_loop* evLoop, Scene& scn, ::Output& out, const DeviceVk& vk, FrameListener& l, const char* font, float scale, int limit) : loop(evLoop), scene(&scn), output(&out), listener(&l), framesLimit(limit), instance(vk.instance), phys(vk.phys), device(vk.device), queueFamily(vk.queueFamily), queue(vk.queue), textureAlloc(pool->make<ObjList<SurfaceTexture>>(pool)), hasDmabuf(vk.hasDmabuf), getMemoryFdProps(vk.getMemoryFdProps) {
            fontPath = font;
            uiScale = scale;
            hasSyncFd = vk.hasSyncFd;
            drmFd = vk.drmFd;
            setup(scn.outW, scn.outH);

            if (out.vsynced()) {
                ev_prepare_init(&prep, prepareCb);
                prep.data = this;
                ev_prepare_start(loop, &prep);
            } else {
                ev_timer_init(&frameTimer, frameTimerCb, 0., 1.0 / scn.hz);
                frameTimer.data = this;
                ev_timer_start(loop, &frameTimer);
            }
        }

        ~RendererImpl() noexcept {
            if (output->vsynced()) {
                ev_prepare_stop(loop, &prep);
            } else {
                ev_timer_stop(loop, &frameTimer);
            }

            shutdown();
        }

        void tick();

        u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags props);
        void createImage(int w, int h, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem);
        void createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf, VkDeviceMemory& mem, void** map);
        void setup(int w, int h);
        void shutdown() noexcept;

        void drawSurfaceTree(Surface& s, float x, float y);
        void drawSurfaceTreeOverlay(Surface& s, float x, float y);
        void markTreeUnhovered(Surface& s);
        void buildUi(Scene& scene);
        void rasterizeShape(int kind, u32* out);

        void motion(double x, double y) override {
            scene->needsFrame = true;
            ImGui::GetIO().AddMousePosEvent((float)x, (float)y);

            // keep the plane position fresh for the next frame commit
            if (hwCursorReady && hwVisible) {
                output->setCursorPos((int)x - hwHotX, (int)y - hwHotY, true);
            }
        }

        void button(u32 btn, bool pressed) override {
            int imguiBtn = btn == BTN_LEFT ? 0 : btn == BTN_RIGHT ? 1 : 2;

            scene->needsFrame = true;
            ImGui::GetIO().AddMouseButtonEvent(imguiBtn, pressed);
        }

        void key(u32, bool) override {
            scene->needsFrame = true;
        }

        void scroll(double dx, double dy) override {
            scene->needsFrame = true;
            ImGui::GetIO().AddMouseWheelEvent((float)-dx, (float)-dy);
        }

        // relative motion and touchpad gestures are for wayland clients only
        void relMotion(double, double, double, double) override {
        }

        void swipeBegin(u32) override {
        }

        void swipeUpdate(double, double) override {
        }

        void swipeEnd(bool) override {
        }

        void pinchBegin(u32) override {
        }

        void pinchUpdate(double, double, double, double) override {
        }

        void pinchEnd(bool) override {
        }

        void holdBegin(u32) override {
        }

        void holdEnd(bool) override {
        }

        bool importDmabuf(Surface& s);
        void uploadSurface(Surface& s);
        void destroyTexture(SurfaceTexture* tex);
        SurfaceTexture* cacheFind(DmabufBuffer* b);
        bool cacheContainsTex(const SurfaceTexture* tex) const;
        void releaseSurfaceTexture(Surface& s);
        void drainDead();

        bool wantFrame() const {
            return scene->needsFrame || settleFrames > 0;
        }

        void frameNow();
        void renderFrame(int scanIdx);
        bool screenshot(const char* path) override;
    };

    void prepareCb(struct ev_loop*, ev_prepare* w, int) {
        auto* r = (RendererImpl*)w->data;

        if (r->wantFrame() && r->output->ready()) {
            r->frameNow();
        }
    }

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
    ici.format = kVkFormat;
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
    scanout = output->scanoutCount() > 0;

    if (!scanout) {
        createImage(width, height, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, target, targetMemory);

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

        vci.image = target;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = kVkFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &vci, nullptr, &targetView));
    }

    createHostBuffer((VkDeviceSize)width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback, readbackMemory, &readbackMap);

    VkAttachmentDescription att{};

    att.format = kVkFormat;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = scanout ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

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

    if (scanout) {
        for (int i = 0; i < output->scanoutCount(); i++) {
            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

            vci.image = output->scanoutBuffer(i)->image;
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = kVkFormat;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkImageView view = VK_NULL_HANDLE;

            VK_CHECK(vkCreateImageView(device, &vci, nullptr, &view));
            scanViews.pushBack(view);

            VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

            fci.renderPass = renderPass;
            fci.attachmentCount = 1;
            fci.pAttachments = &view;
            fci.width = width;
            fci.height = height;
            fci.layers = 1;

            VkFramebuffer fb = VK_NULL_HANDLE;

            VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &fb));
            scanFbs.pushBack(fb);
        }
    } else {
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

        fci.renderPass = renderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &targetView;
        fci.width = width;
        fci.height = height;
        fci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &framebuffer));
    }

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

    if (hasSyncFd) {
        importSemFd = (PFN_vkImportSemaphoreFdKHR)vkGetDeviceProcAddr(device, "vkImportSemaphoreFdKHR");
        getSemFd = (PFN_vkGetSemaphoreFdKHR)vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR");
        hasSyncFd = importSemFd && getSemFd;
    }

    if (hasSyncFd) {
        VkExportSemaphoreCreateInfo exp{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};

        exp.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

        VkSemaphoreCreateInfo sci2{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        sci2.pNext = &exp;
        VK_CHECK(vkCreateSemaphore(device, &sci2, nullptr, &syncOut));

        VkSemaphoreCreateInfo plain{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        for (auto& sem : syncWaitPool) {
            VK_CHECK(vkCreateSemaphore(device, &plain, nullptr, &sem));
        }
    }

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

    const char* fontCandidates[] = {fontPath, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"};

    for (const char* f : fontCandidates) {
        if (f && access(f, R_OK) == 0 && io.Fonts->AddFontFromFileTTF(f, 16.f, nullptr, io.Fonts->GetGlyphRangesCyrillic())) {
            break;
        }
    }

    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    if (uiScale != 1.f) {
        ImGuiStyle& st = ImGui::GetStyle();

        st.FontScaleMain = uiScale;
        st.ScaleAllSizes(uiScale);
    }

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

    hwCapW = output->cursorCapW();
    hwCapH = output->cursorCapH();

    if (hwCapW > 0 && hwCapH > 0) {
        createImage(hwCapW, hwCapH, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, curImg, curImgMem);

        VkImageViewCreateInfo cvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

        cvi.image = curImg;
        cvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        cvi.format = kVkFormat;
        cvi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &cvi, nullptr, &curView));

        VkFramebufferCreateInfo cfi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

        cfi.renderPass = renderPass;
        cfi.attachmentCount = 1;
        cfi.pAttachments = &curView;
        cfi.width = (u32)hwCapW;
        cfi.height = (u32)hwCapH;
        cfi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &cfi, nullptr, &curFb));

        createHostBuffer((VkDeviceSize)hwCapW * hwCapH * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, curReadback, curReadbackMem, &curReadbackMap);

        VkCommandBufferAllocateInfo cba{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

        cba.commandPool = cmdPool;
        cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cba.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device, &cba, &curCmd));

        VkFenceCreateInfo cfe{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

        VK_CHECK(vkCreateFence(device, &cfe, nullptr, &curFence));
        hwCursorReady = true;
    }
}

void RendererImpl::uploadSurface(Surface& s) {
    if (s.width <= 0 || s.height <= 0) {
        return;
    }

    SurfaceTexture* tex = s.texture;

    if (tex && tex->external) {
        releaseSurfaceTexture(s);
        tex = nullptr;
    }

    if (tex && (tex->w != s.width || tex->h != s.height)) {
        destroyTexture(tex);
        tex = nullptr;
        s.texture = nullptr;
    }

    bool fresh = tex == nullptr;

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
        vci.format = kVkFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vci, nullptr, &tex->view);
        tex->ds = ImGui_ImplVulkan_AddTexture(sampler, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        textures.pushBack(tex);
        s.texture = tex;
    }

    RectI r{0, 0, tex->w, tex->h};

    if (!fresh && !s.damageAll && !s.damage.empty()) {
        r = s.damage;
        clipRect(r, tex->w, tex->h);
    }

    if (r.x == 0 && r.y == 0 && r.w == tex->w && r.h == tex->h) {
        memcpy(tex->stagingMap, s.pixels.data(), s.pixels.length());
    } else {
        for (i32 y = r.y; y < r.y + r.h; y++) {
            size_t off = ((size_t)y * tex->w + r.x) * 4;

            memcpy((u8*)tex->stagingMap + off, s.pixels.data() + off, (size_t)r.w * 4);
        }
    }

    unionRect(tex->uploadRect, r);
    tex->needsUpload = true;
    s.damage = {};
    s.damageAll = false;
}

SurfaceTexture* RendererImpl::cacheFind(DmabufBuffer* b) {
    for (const auto& e : dmabufCache) {
        if (e.key == b) {
            return e.tex;
        }
    }

    return nullptr;
}

bool RendererImpl::cacheContainsTex(const SurfaceTexture* tex) const {
    for (const auto& e : dmabufCache) {
        if (e.tex == tex) {
            return true;
        }
    }

    return false;
}

void RendererImpl::releaseSurfaceTexture(Surface& s) {
    SurfaceTexture* tex = s.texture;

    s.texture = nullptr;

    if (!tex) {
        return;
    }

    if (tex->external && cacheContainsTex(tex)) {
        return;
    }

    destroyTexture(tex);
}

void RendererImpl::drainDead() {
    while (!scene->orphanedTextures.empty()) {
        SurfaceTexture* t = scene->orphanedTextures.popBack();

        if (!cacheContainsTex(t)) {
            destroyTexture(t);
        }
    }

    while (!scene->deadDmabufs.empty()) {
        DmabufBuffer* b = scene->deadDmabufs.popBack();
        SurfaceTexture* tex = cacheFind(b);

        if (!tex) {
            continue;
        }

        bool inUse = false;

        for (Surface* s : scene->surfaces) {
            if (s->texture == tex) {
                inUse = true;
            }
        }

        for (size_t i = 0; i < dmabufCache.length(); i++) {
            if (dmabufCache[i].key == b) {
                dmabufCache.mut(i) = dmabufCache.back();
                dmabufCache.popBack();

                break;
            }
        }

        if (!inUse) {
            destroyTexture(tex);
        }
    }
}

void RendererImpl::destroyTexture(SurfaceTexture* tex) {
    if (!tex) {
        return;
    }

    for (size_t i = 0; i < dmabufCache.length(); i++) {
        if (dmabufCache[i].tex == tex) {
            dmabufCache.mut(i) = dmabufCache.back();
            dmabufCache.popBack();

            break;
        }
    }

    if (tex->ds) {
        ImGui_ImplVulkan_RemoveTexture(tex->ds);
    }

    for (VkDeviceMemory m : tex->extraMemory) {
        if (m) {
            vkFreeMemory(device, m, nullptr);
        }
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

    if (b->nplanes < 1 || b->nplanes > kDmabufMaxPlanes) {
        return false;
    }

    SurfaceTexture* cached = cacheFind(b);

    if (s.texture && s.texture != cached) {
        releaseSurfaceTexture(s);
    }

    if (cached) {
        s.texture = cached;

        return true;
    }

    auto* tex = textureAlloc->make();

    tex->w = b->width;
    tex->h = b->height;
    tex->external = true;

    VkSubresourceLayout planes[kDmabufMaxPlanes] = {};
    bool disjoint = false;

    for (int i = 0; i < b->nplanes; i++) {
        planes[i].offset = b->offsets[i];
        planes[i].rowPitch = b->strides[i];

        if (b->fds[i] != b->fds[0]) {
            disjoint = true;
        }
    }

    VkImageDrmFormatModifierExplicitCreateInfoEXT modInfo{
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};

    modInfo.drmFormatModifier = b->modifier;
    modInfo.drmFormatModifierPlaneCount = (u32)b->nplanes;
    modInfo.pPlaneLayouts = planes;

    VkExternalMemoryImageCreateInfo extInfo{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};

    extInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    extInfo.pNext = &modInfo;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};

    ici.pNext = &extInfo;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = kVkFormat;
    ici.extent = {(u32)b->width, (u32)b->height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (disjoint) {
        ici.flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
    }

    if (vkCreateImage(device, &ici, nullptr, &tex->image) != VK_SUCCESS) {
        sysE << "imway: dmabuf vkCreateImage failed"_sv << endL;
        textureAlloc->release(tex);

        return false;
    }

    auto pickType = [&](u32 bits, int fd) -> u32 {
        VkMemoryFdPropertiesKHR fdProps{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};

        if (getMemoryFdProps(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdProps) != VK_SUCCESS) {
            return UINT32_MAX;
        }

        u32 typeBits = bits & fdProps.memoryTypeBits;

        for (u32 i = 0; i < 32; i++) {
            if (typeBits & (1u << i)) {
                return i;
            }
        }

        return UINT32_MAX;
    };

    bool bound = false;

    if (!disjoint) {
        int fd = dup(b->fds[0]);
        VkMemoryRequirements req{};

        vkGetImageMemoryRequirements(device, tex->image, &req);

        u32 memType = pickType(req.memoryTypeBits, fd);

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
        bound = memType != UINT32_MAX && vkAllocateMemory(device, &mai, nullptr, &tex->memory) == VK_SUCCESS && vkBindImageMemory(device, tex->image, tex->memory, 0) == VK_SUCCESS;

        if (!bound) {
            close(fd);
        }
    } else {
        constexpr VkImageAspectFlagBits kPlaneAspects[kDmabufMaxPlanes] = {VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT, VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT, VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT, VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT};
        VkBindImageMemoryInfo binds[kDmabufMaxPlanes] = {};
        VkBindImagePlaneMemoryInfo planeInfos[kDmabufMaxPlanes] = {};

        bound = true;

        for (int i = 0; i < b->nplanes && bound; i++) {
            VkImagePlaneMemoryRequirementsInfo planeReq{VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO};

            planeReq.planeAspect = kPlaneAspects[i];

            VkImageMemoryRequirementsInfo2 ri{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};

            ri.pNext = &planeReq;
            ri.image = tex->image;

            VkMemoryRequirements2 req2{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};

            vkGetImageMemoryRequirements2(device, &ri, &req2);

            int fd = dup(b->fds[i]);
            u32 memType = pickType(req2.memoryRequirements.memoryTypeBits, fd);

            VkImportMemoryFdInfoKHR importInfo{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};

            importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            importInfo.fd = fd;

            VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

            mai.pNext = &importInfo;
            mai.allocationSize = req2.memoryRequirements.size;
            mai.memoryTypeIndex = memType;

            VkDeviceMemory mem = VK_NULL_HANDLE;

            if (memType == UINT32_MAX || vkAllocateMemory(device, &mai, nullptr, &mem) != VK_SUCCESS) {
                close(fd);
                bound = false;

                break;
            }

            if (i == 0) {
                tex->memory = mem;
            } else {
                tex->extraMemory[i - 1] = mem;
            }

            planeInfos[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
            planeInfos[i].planeAspect = kPlaneAspects[i];
            binds[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
            binds[i].pNext = &planeInfos[i];
            binds[i].image = tex->image;
            binds[i].memory = mem;
        }

        bound = bound && vkBindImageMemory2(device, (u32)b->nplanes, binds) == VK_SUCCESS;
    }

    if (!bound) {
        sysE << "imway: dmabuf memory import failed ("_sv << b->nplanes << " planes)"_sv << endL;
        vkDestroyImage(device, tex->image, nullptr);

        if (tex->memory) {
            vkFreeMemory(device, tex->memory, nullptr);
        }

        for (VkDeviceMemory m : tex->extraMemory) {
            if (m) {
                vkFreeMemory(device, m, nullptr);
            }
        }

        textureAlloc->release(tex);

        return false;
    }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    vci.image = tex->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = kVkFormat;
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
    dmabufCache.pushBack({b, tex});
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

namespace {
    // vector mouse cursor: the atlas-baked imgui one is a 12x19 bitmap and turns
    // to mush when scaled, so draw crisp shapes at any --scale instead
    void cursorPoly(ImDrawList* dl, const ImVec2* src, int n, ImVec2 p, float s, float rc, float rs) {
        ImVec2 pts[16], sh[16];

        for (int i = 0; i < n; i++) {
            float x = src[i].x * rc - src[i].y * rs;
            float y = src[i].x * rs + src[i].y * rc;

            pts[i] = ImVec2(p.x + x * s, p.y + y * s);
            sh[i] = ImVec2(pts[i].x + 1.5f * s, pts[i].y + 1.5f * s);
        }

        dl->AddConcavePolyFilled(sh, n, IM_COL32(0, 0, 0, 48));
        dl->AddConcavePolyFilled(pts, n, IM_COL32_WHITE);
        dl->AddPolyline(pts, n, IM_COL32_BLACK, ImDrawFlags_Closed, s > 1.f ? 1.2f * s : 1.2f);
    }

    void cursorStroke(ImDrawList* dl, ImVec2 p, float s, float x0, float y0, float x1, float y1, ImU32 col, float th) {
        dl->AddLine(ImVec2(p.x + x0 * s, p.y + y0 * s), ImVec2(p.x + x1 * s, p.y + y1 * s), col, th * s);
    }

    void drawMouseCursor(ImDrawList* dl, ImVec2 p, float s, ImGuiMouseCursor c) {
        // NB: AddConcavePolyFilled wants this winding, reversed it fills the convex hull
        static const ImVec2 arrow[] = {{12.2f, 11.8f}, {6.8f, 11.8f}, {9.5f, 17.8f}, {6.9f, 18.9f}, {4.2f, 12.9f}, {0.f, 16.5f}, {0.f, 0.f}};
        static const ImVec2 ns[] = {{0.f, -9.f}, {4.5f, -4.5f}, {1.7f, -4.5f}, {1.7f, 4.5f}, {4.5f, 4.5f}, {0.f, 9.f}, {-4.5f, 4.5f}, {-1.7f, 4.5f}, {-1.7f, -4.5f}, {-4.5f, -4.5f}};
        static const ImVec2 hand[] = {{0.f, 0.f}, {2.6f, 0.f}, {2.6f, 5.5f}, {4.f, 4.f}, {6.f, 4.f}, {7.6f, 5.6f}, {7.6f, 10.f}, {5.6f, 14.5f}, {-0.5f, 14.5f}, {-3.4f, 10.5f}, {-3.4f, 6.5f}, {-1.6f, 4.8f}, {0.f, 6.f}};
        const float R = 0.70710678f;

        switch (c) {
            case ImGuiMouseCursor_None:
                return;
            case ImGuiMouseCursor_TextInput:
                cursorStroke(dl, p, s, -2.5f, -8.f, 2.5f, -8.f, IM_COL32_BLACK, 3.4f);
                cursorStroke(dl, p, s, 0.f, -8.f, 0.f, 8.f, IM_COL32_BLACK, 3.4f);
                cursorStroke(dl, p, s, -2.5f, 8.f, 2.5f, 8.f, IM_COL32_BLACK, 3.4f);
                cursorStroke(dl, p, s, -2.5f, -8.f, 2.5f, -8.f, IM_COL32_WHITE, 1.4f);
                cursorStroke(dl, p, s, 0.f, -8.f, 0.f, 8.f, IM_COL32_WHITE, 1.4f);
                cursorStroke(dl, p, s, -2.5f, 8.f, 2.5f, 8.f, IM_COL32_WHITE, 1.4f);
                return;
            case ImGuiMouseCursor_ResizeNS:
                cursorPoly(dl, ns, 10, p, s, 1.f, 0.f);
                return;
            case ImGuiMouseCursor_ResizeEW:
                cursorPoly(dl, ns, 10, p, s, 0.f, 1.f);
                return;
            case ImGuiMouseCursor_ResizeNESW:
                cursorPoly(dl, ns, 10, p, s, R, -R);
                return;
            case ImGuiMouseCursor_ResizeNWSE:
                cursorPoly(dl, ns, 10, p, s, R, R);
                return;
            case ImGuiMouseCursor_ResizeAll:
                cursorPoly(dl, ns, 10, p, s, 1.f, 0.f);
                cursorPoly(dl, ns, 10, p, s, 0.f, 1.f);
                return;
            case ImGuiMouseCursor_Hand:
                cursorPoly(dl, hand, 13, p, s, 1.f, 0.f);
                return;
            case ImGuiMouseCursor_NotAllowed:
                dl->AddCircle(p, 7.5f * s, IM_COL32_BLACK, 0, 4.f * s);
                dl->AddCircle(p, 7.5f * s, IM_COL32_WHITE, 0, 2.f * s);
                cursorStroke(dl, p, s, -5.3f, -5.3f, 5.3f, 5.3f, IM_COL32_BLACK, 4.f);
                cursorStroke(dl, p, s, -5.3f, -5.3f, 5.3f, 5.3f, IM_COL32_WHITE, 2.f);
                return;
            case ImGuiMouseCursor_Wait:
            case ImGuiMouseCursor_Progress: {
                float a0 = (float)(nowMsec() % 1000) * 0.0062831853f;
                float a1 = a0 + 5.2f;

                if (c == ImGuiMouseCursor_Progress) {
                    cursorPoly(dl, arrow, 7, p, s, 1.f, 0.f);
                    p = ImVec2(p.x + 14.f * s, p.y - 1.f * s);
                }

                dl->PathArcTo(p, 6.5f * s, a0, a1);
                dl->PathStroke(IM_COL32_BLACK, ImDrawFlags_None, 3.6f * s);
                dl->PathArcTo(p, 6.5f * s, a0, a1);
                dl->PathStroke(IM_COL32_WHITE, ImDrawFlags_None, 1.8f * s);
                return;
            }
            default:
                cursorPoly(dl, arrow, 7, p, s, 1.f, 0.f);
                return;
        }
    }
}

// one-off offscreen render of a cursor shape into a premultiplied ARGB image
// for the KMS cursor plane; the hotspot lands at the canvas center
void RendererImpl::rasterizeShape(int kind, u32* out) {
    ImDrawList dl(ImGui::GetDrawListSharedData());

    dl._ResetForNewFrame();
    dl.PushClipRect(ImVec2(0.f, 0.f), ImVec2((float)hwCapW, (float)hwCapH), false);

    float s = uiScale;
    float half = (float)(hwCapW < hwCapH ? hwCapW : hwCapH) * 0.5f;

    // the plane cannot scale: clamp so the largest shape fits the buffer
    if (21.f * s > half - 2.f) {
        s = (half - 2.f) / 21.f;
    }

    drawMouseCursor(&dl, ImVec2((float)hwCapW * 0.5f, (float)hwCapH * 0.5f), s, kind);
    dl.PopClipRect();

    ImDrawData dd;

    dd.Valid = true;
    dd.CmdLists.push_back(&dl);
    dd.CmdListsCount = 1;
    dd.TotalVtxCount = dl.VtxBuffer.Size;
    dd.TotalIdxCount = dl.IdxBuffer.Size;
    dd.DisplayPos = ImVec2(0.f, 0.f);
    dd.DisplaySize = ImVec2((float)hwCapW, (float)hwCapH);
    dd.FramebufferScale = ImVec2(1.f, 1.f);
    dd.Textures = &ImGui::GetPlatformIO().Textures;

    // the imgui backend cycles shared vertex buffers; make sure no frame is in flight
    vkQueueWaitIdle(queue);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(curCmd, 0);
    vkBeginCommandBuffer(curCmd, &bi);

    VkClearValue clear{};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};

    rbi.renderPass = renderPass;
    rbi.framebuffer = curFb;
    rbi.renderArea = {{0, 0}, {(u32)hwCapW, (u32)hwCapH}};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(curCmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(&dd, curCmd);
    vkCmdEndRenderPass(curCmd);

    VkImageLayout layout = scanout ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.oldLayout = layout;
    bar.newLayout = layout;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = curImg;
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(curCmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

    VkBufferImageCopy region{};

    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {(u32)hwCapW, (u32)hwCapH, 1};
    vkCmdCopyImageToBuffer(curCmd, curImg, layout, curReadback, 1, &region);
    vkEndCommandBuffer(curCmd);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

    si.commandBufferCount = 1;
    si.pCommandBuffers = &curCmd;
    vkQueueSubmit(queue, 1, &si, curFence);
    vkWaitForFences(device, 1, &curFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &curFence);

    // B8G8R8A8 bytes match DRM ARGB8888, and rendering onto transparent
    // black with src-alpha blending yields premultiplied pixels
    memcpy(out, curReadbackMap, (size_t)hwCapW * hwCapH * 4);
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

    if (moving && !contains(scene.toplevels, moving)) {
        moving = nullptr;
    }

    if (resizing && !contains(scene.toplevels, resizing)) {
        resizing = nullptr;
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

        StringView title(t->title);

        if (title.length() > 280) {
            title = title.prefix(280);
        }

        CStr<320> label;

        label << title << "###toplevel"_sv << (u64)t->id;
        ImGui::SetNextWindowPos(ImVec2(40.f + 30.f * i, 60.f + 30.f * i), ImGuiCond_FirstUseEver);
        i++;

        if (!t->winSizeSet) {
            const ImGuiStyle& st = ImGui::GetStyle();

            ImGui::SetNextWindowSize(ImVec2((float)root->viewW() + st.WindowPadding.x * 2, (float)root->viewH() + st.WindowPadding.y * 2 + ImGui::GetFrameHeight()), ImGuiCond_Always);
            t->winSizeSet = true;
        }

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        if (t->fullscreen) {
            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2((float)width, (float)height), ImGuiCond_Always);
            flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
        }

        if (ImGui::Begin(label.cStr(), nullptr, flags)) {
            if (t->raiseRequested) {
                t->raiseRequested = false;
                ImGui::SetWindowFocus();
            }

            if (t->moveRequested) {
                t->moveRequested = false;

                if (ImGui::IsAnyMouseDown()) {
                    moving = t;
                    resizing = nullptr;

                    ImVec2 wp = ImGui::GetWindowPos();

                    moveOff = ImVec2(wp.x - io.MousePos.x, wp.y - io.MousePos.y);
                }
            }

            if (t->resizeEdges) {
                if (ImGui::IsAnyMouseDown()) {
                    resizing = t;
                    moving = nullptr;
                    activeEdges = t->resizeEdges;
                    resizeStartSz = ImGui::GetWindowSize();
                    resizeStartPos = ImGui::GetWindowPos();
                    resizeStartMouse = io.MousePos;
                }

                t->resizeEdges = 0;
            }

            if (moving == t) {
                if (ImGui::IsAnyMouseDown()) {
                    ImGui::SetWindowPos(ImVec2(io.MousePos.x + moveOff.x, io.MousePos.y + moveOff.y));
                    scene.needsFrame = true;
                } else {
                    moving = nullptr;
                }
            }

            if (resizing == t) {
                if (ImGui::IsAnyMouseDown()) {
                    float dx = io.MousePos.x - resizeStartMouse.x;
                    float dy = io.MousePos.y - resizeStartMouse.y;
                    ImVec2 sz = resizeStartSz;
                    ImVec2 pos = resizeStartPos;

                    if (activeEdges & 8) {
                        sz.x += dx;
                    } else if (activeEdges & 4) {
                        sz.x -= dx;
                        pos.x += dx;
                    }

                    if (activeEdges & 2) {
                        sz.y += dy;
                    } else if (activeEdges & 1) {
                        sz.y -= dy;
                        pos.y += dy;
                    }

                    sz.x = sz.x < 120.f ? 120.f : sz.x;
                    sz.y = sz.y < 80.f ? 80.f : sz.y;
                    ImGui::SetWindowSize(sz);

                    if (activeEdges & 5) {
                        ImGui::SetWindowPos(pos);
                    }

                    scene.needsFrame = true;
                } else {
                    resizing = nullptr;
                }
            }

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
    if (scene.dragIcon && scene.dragIcon->texture) {
        ImVec2 mp = ImGui::GetMousePos();

        drawSurfaceTreeOverlay(*scene.dragIcon, mp.x + 4, mp.y + 4);
    }

    bool overClient = false;

    for (Surface* s : scene.surfaces) {
        if (s->hovered) {
            overClient = true;
        }
    }

    Surface* cs = overClient && scene.cursorSurface && scene.cursorSurface->texture ? scene.cursorSurface : nullptr;

    if (cs) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    } else if (overClient && scene.cursorShape != CursorKind::unset) {
        ImGuiMouseCursor c = ImGuiMouseCursor_Arrow;

        switch (scene.cursorShape) {
            case CursorKind::hidden: c = ImGuiMouseCursor_None; break;
            case CursorKind::text: c = ImGuiMouseCursor_TextInput; break;
            case CursorKind::hand: c = ImGuiMouseCursor_Hand; break;
            case CursorKind::grab: c = ImGuiMouseCursor_Hand; break;
            case CursorKind::move: c = ImGuiMouseCursor_ResizeAll; break;
            case CursorKind::nsResize: c = ImGuiMouseCursor_ResizeNS; break;
            case CursorKind::ewResize: c = ImGuiMouseCursor_ResizeEW; break;
            case CursorKind::neswResize: c = ImGuiMouseCursor_ResizeNESW; break;
            case CursorKind::nwseResize: c = ImGuiMouseCursor_ResizeNWSE; break;
            case CursorKind::notAllowed: c = ImGuiMouseCursor_NotAllowed; break;
            case CursorKind::wait: c = ImGuiMouseCursor_Wait; break;
            default: break;
        }

        ImGui::SetMouseCursor(c);
    }

    if (!scene.drawCursor) {
        return;
    }

    ImVec2 mp = ImGui::GetMousePos();
    int kind = ImGui::GetMouseCursor();

    // the plane can get rejected at runtime (mode-dependent), re-check live
    bool hwCursor = hwCursorReady && output->cursorCapW() > 0;

    if (cs) {
        // client-provided cursor surface: feed its pixels to the cursor plane
        bool hwOk = hwCursor && !cs->dmabuf && cs->width > 0 && cs->height > 0 && cs->width <= hwCapW && cs->height <= hwCapH && cs->pixels.length() >= (size_t)cs->width * cs->height * 4;

        if (hwOk) {
            if (hwSurf != cs || hwSurfStale) {
                hwScratch.zero((size_t)hwCapW * hwCapH);

                for (int y = 0; y < cs->height; y++) {
                    memcpy(hwScratch.mutData() + (size_t)y * hwCapW, (const u32*)cs->pixels.data() + (size_t)y * cs->width, (size_t)cs->width * 4);
                }

                output->setCursorImage(hwScratch.data());
                hwSurf = cs;
                hwKind = -3;
                hwSurfStale = false;
            }

            hwHotX = scene.cursorHotX;
            hwHotY = scene.cursorHotY;
            hwVisible = true;
            output->setCursorPos((int)mp.x - hwHotX, (int)mp.y - hwHotY, true);
        } else {
            if (hwCursor) {
                hwVisible = false;
                output->setCursorPos(0, 0, false);
            }

            drawSurfaceTreeOverlay(*cs, mp.x - scene.cursorHotX, mp.y - scene.cursorHotY);
        }

        return;
    }

    if (!hwCursor) {
        hwVisible = false;

        if (kind != ImGuiMouseCursor_None) {
            drawMouseCursor(ImGui::GetForegroundDrawList(), mp, uiScale, kind);
        }

        return;
    }

    if (kind == ImGuiMouseCursor_None) {
        hwVisible = false;
        output->setCursorPos(0, 0, false);

        return;
    }

    if (kind < 0 || kind >= ImGuiMouseCursor_COUNT) {
        kind = ImGuiMouseCursor_Arrow;
    }

    if (hwKind != kind) {
        Vector<u32>& img = hwShapeCache[kind];

        if (!img.length()) {
            img.zero((size_t)hwCapW * hwCapH);
            rasterizeShape(kind, img.mutData());
        }

        output->setCursorImage(img.data());
        hwKind = kind;
        hwSurf = nullptr;
    }

    hwHotX = hwCapW / 2;
    hwHotY = hwCapH / 2;
    hwVisible = true;
    output->setCursorPos((int)mp.x - hwHotX, (int)mp.y - hwHotY, true);
}

void RendererImpl::renderFrame(int scanIdx) {
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

        RectI r = tex->uploadRect;

        if (r.empty()) {
            r = {0, 0, tex->w, tex->h};
        }

        VkBufferImageCopy region{};

        region.bufferOffset = ((VkDeviceSize)r.y * tex->w + r.x) * 4;
        region.bufferRowLength = (u32)tex->w;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {r.x, r.y, 0};
        region.imageExtent = {(u32)r.w, (u32)r.h, 1};
        vkCmdCopyBufferToImage(cmd, tex->staging, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        tex->uploadRect = {};

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
    rbi.framebuffer = scanIdx >= 0 ? scanFbs[scanIdx] : framebuffer;
    rbi.renderArea = {{0, 0}, {(u32)width, (u32)height}};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);

    lastImage = scanIdx >= 0 ? output->scanoutBuffer(scanIdx)->image : target;
    lastLayout = scanIdx >= 0 ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    haveFrame = true;

    if (output->presentNeedsPixels()) {
        VkBufferImageCopy region{};

        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(u32)width, (u32)height, 1};
        vkCmdCopyImageToBuffer(cmd, lastImage, lastLayout, readback, 1, &region);
    }

    vkEndCommandBuffer(cmd);

    frameSyncFds.clear();

    VkSemaphore waits[16];
    VkPipelineStageFlags waitStages[16];
    u32 nwaits = 0;

    if (hasSyncFd) {
        for (Surface* s : scene->surfaces) {
            if (!s->dmabuf || !s->texture || !s->texture->external) {
                continue;
            }

            for (int i = 0; i < s->dmabuf->nplanes; i++) {
                int fd = s->dmabuf->fds[i];

                if (fd < 0 || contains(frameSyncFds, fd)) {
                    continue;
                }

                frameSyncFds.pushBack(fd);

                if (nwaits >= 16) {
                    continue;
                }

                dma_buf_export_sync_file exp{};

                exp.flags = DMA_BUF_SYNC_WRITE;
                exp.fd = -1;

                if (ioctl(fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exp) != 0 || exp.fd < 0) {
                    continue;
                }

                VkImportSemaphoreFdInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};

                imp.semaphore = syncWaitPool[nwaits];
                imp.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
                imp.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
                imp.fd = exp.fd;

                if (importSemFd(device, &imp) != VK_SUCCESS) {
                    close(exp.fd);

                    continue;
                }

                waits[nwaits] = syncWaitPool[nwaits];
                waitStages[nwaits] = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                nwaits++;
            }
        }
    }

    bool signalOut = hasSyncFd && !frameSyncFds.empty();

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.waitSemaphoreCount = nwaits;
    si.pWaitSemaphores = waits;
    si.pWaitDstStageMask = waitStages;
    si.signalSemaphoreCount = signalOut ? 1 : 0;
    si.pSignalSemaphores = &syncOut;
    vkQueueSubmit(queue, 1, &si, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);

    if (signalOut) {
        VkSemaphoreGetFdInfoKHR gfi{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};

        gfi.semaphore = syncOut;
        gfi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

        int outFd = -1;

        if (getSemFd(device, &gfi, &outFd) == VK_SUCCESS && outFd >= 0) {
            for (int fd : frameSyncFds) {
                dma_buf_import_sync_file imp{};

                imp.flags = DMA_BUF_SYNC_READ;
                imp.fd = outFd;
                ioctl(fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &imp);
            }

            close(outFd);
        }
    }
}

bool RendererImpl::screenshot(const char* path) {
    if (!haveFrame) {
        return false;
    }

    if (!output->presentNeedsPixels()) {
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkBufferImageCopy region{};

        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(u32)width, (u32)height, 1};
        vkCmdCopyImageToBuffer(cmd, lastImage, lastLayout, readback, 1, &region);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(queue, 1, &si, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fence);
    }

    ScopedFD f(open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));

    if (f.get() < 0) {
        return false;
    }

    auto writeAll = [&f](const void* data, size_t len) {
        auto* b = (const u8*)data;

        while (len) {
            size_t n = f.write(b, len);

            b += n;
            len -= n;
        }
    };

    StringBuilder ppm;

    ppm << "P6\n"_sv << width << " "_sv << height << "\n255\n"_sv;
    writeAll(ppm.data(), StringView(ppm).length());

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

        writeAll(row.data(), row.length());
    }

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

    if (syncOut) {
        vkDestroySemaphore(device, syncOut, nullptr);
    }

    for (VkSemaphore sem : syncWaitPool) {
        if (sem) {
            vkDestroySemaphore(device, sem, nullptr);
        }
    }

    if (curFence) {
        vkDestroyFence(device, curFence, nullptr);
    }

    if (curFb) {
        vkDestroyFramebuffer(device, curFb, nullptr);
    }

    if (curView) {
        vkDestroyImageView(device, curView, nullptr);
    }

    if (curImg) {
        vkDestroyImage(device, curImg, nullptr);
    }

    if (curImgMem) {
        vkFreeMemory(device, curImgMem, nullptr);
    }

    if (curReadbackMap) {
        vkUnmapMemory(device, curReadbackMem);
    }

    if (curReadback) {
        vkDestroyBuffer(device, curReadback, nullptr);
    }

    if (curReadbackMem) {
        vkFreeMemory(device, curReadbackMem, nullptr);
    }

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr);

    for (VkFramebuffer fb : scanFbs) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }

    for (VkImageView v : scanViews) {
        vkDestroyImageView(device, v, nullptr);
    }

    vkDestroyRenderPass(device, renderPass, nullptr);

    if (readbackMap) {
        vkUnmapMemory(device, readbackMemory);
    }

    vkDestroyBuffer(device, readback, nullptr);
    vkFreeMemory(device, readbackMemory, nullptr);
    vkDestroyImageView(device, targetView, nullptr);
    vkDestroyImage(device, target, nullptr);
    vkFreeMemory(device, targetMemory, nullptr);
    device = VK_NULL_HANDLE;
}

void RendererImpl::frameNow() {
    if (scene->needsFrame) {
        settleFrames = 3;
    }

    scene->needsFrame = false;
    settleFrames--;

    drainDead();

    for (Surface* s : scene->surfaces) {
        if (s->syncAcquireWait && drmFd >= 0) {
            // explicit sync: the client told us when the buffer is ready
            timespec ts{};

            clock_gettime(CLOCK_MONOTONIC, &ts);

            i64 deadline = (i64)ts.tv_sec * 1000000000 + ts.tv_nsec + 200000000;
            u32 h = s->syncAcquireHandle;
            u64 pt = s->syncAcquirePoint;

            if (drmSyncobjTimelineWait(drmFd, &h, &pt, 1, deadline, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, nullptr) != 0) {
                sysE << "imway: acquire point wait timed out"_sv << endL;
            }

            s->syncAcquireWait = false;
        }

        if (s->dirty && s->hasContent) {
            if (s->dmabuf) {
                importDmabuf(*s);
            } else {
                uploadSurface(*s);
            }

            if (s == scene->cursorSurface) {
                hwSurfStale = true;
            }

            s->dirty = false;
        }
    }

    int idx = output->scanoutCount() > 0 ? output->acquire() : -1;

    renderFrame(idx);

    if (idx >= 0) {
        output->presentImage(idx);
    } else {
        output->present(output->presentNeedsPixels() ? readbackMap : nullptr);
    }

    if (listener) {
        listener->frameShown(nowMsec());
    }

    scene->framesDone++;

    if (framesLimit > 0 && scene->framesDone >= framesLimit) {
        ev_break(loop, EVBREAK_ALL);
    }
}

void RendererImpl::tick() {
    if (wantFrame()) {
        frameNow();

        return;
    }

    scene->framesDone++;

    if (framesLimit > 0 && scene->framesDone >= framesLimit) {
        ev_break(loop, EVBREAK_ALL);
    }
}

Renderer* Renderer::create(ObjPool* pool, struct ev_loop* loop, Scene& scene, ::Output& output, const DeviceVk& vk, FrameListener& listener, const char* fontPath, float uiScale, int framesLimit) {
    return pool->make<RendererImpl>(pool, loop, scene, output, vk, listener, fontPath, uiScale, framesLimit);
}
