#include "renderer.h"

#include "log.h"
#include "osd.h"
#include "icon.h"
#include "util.h"
#include "wifi.h"
#include "color.h"
#include "input.h"
#include "mixer.h"
#include "scene.h"
#include "toast.h"
#include "dialog.h"
#include "output.h"
#include "pooled.h"
#include "desktop.h"
#include "history.h"
#include "wayland.h"
#include "wifi_ui.h"
#include "calendar.h"
#include "composer.h"
#include "imgui_wm.h"
#include "launcher.h"
#include "listener.h"
#include "log_view.h"
#include "notifier.h"
#include "settings.h"
#include "tex_pool.h"
#include "device_vk.h"
#include "icon_pool.h"
#include "inspector.h"
#include "intr_list.h"
#include "pooled_ev.h"
#include "anr_dialog.h"
#include "fence_poll.h"
#include "input_sink.h"
#include "lock_screen.h"
#include "frame_capture.h"
#include "render_filter.h"
#include "window_shadow.h"
#include "desktop_chrome.h"
#include "frame_listener.h"
#include "main_supervisor.h"
#include "screenshot_capture.h"
#include "small_obj_allocator.h"

#include <std/sys/fd.h>
#include <std/sys/fs.h>
#include <std/ios/sys.h>
#include <std/dbg/verify.h>
#include <std/ios/out_fd.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/ios/fs_utils.h>
#include <std/mem/obj_pool.h>
#include <std/rng/split_mix_64.h>

#include <ev.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#include <vulkan/vulkan.h>
#include <fullscreen.spv.h>
#include <imgui_impl_vulkan.h>
#include <renderer_scene.spv.h>
#include <renderer_cursor.spv.h>
#include <renderer_output.spv.h>
#include <linux/input-event-codes.h>
#include <xdg-shell-server-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>

using namespace stl;

struct TextureLease;

// the node links it into the renderer's texture registry
struct SurfaceTexture: stl::IntrusiveNode {
    // weak-ring anchor: destroyTexture invalidates it, nulling every
    // Surface::texture and lease back-pointer aimed here
    Weak<SurfaceTexture> weak;

    int w = 0, h = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceMemory extraMemory[3] = {};
    VkImageView view = VK_NULL_HANDLE;
    VkImageView chromaView = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingMap = nullptr;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    // which pool in the renderer's growable chain ds was allocated from, so it
    // can be returned to the right pool on teardown
    VkDescriptorPool dsPool = VK_NULL_HANDLE;
    RectI uploadRect;
    u32 mips = 1;
    bool needsUpload = false;
    bool firstUse = true;
    bool external = false;
    bool arenaOwned = false;
};

// arena-death hook: destroys its texture unless destroyTexture already ran
// (the weak ring nulls the pointer then)
struct TextureLease {
    void* owner = nullptr;
    Weak<SurfaceTexture> texture;
    void (*destroy)(void*, SurfaceTexture*) = nullptr;

    TextureLease(void* o, SurfaceTexture* t, void (*d)(void*, SurfaceTexture*));
    ~TextureLease() noexcept;
};

namespace {
    // the imgui vulkan backend swallows every VkResult unless this hook is
    // set (it only ever calls it, never acts on the code): an exhausted
    // descriptor pool would otherwise return VK_ERROR_OUT_OF_POOL_MEMORY,
    // leave the descriptor set uninitialized, and segfault in the driver on
    // the next vkUpdateDescriptorSets. Abort at the failing call instead.
    void imguiVkCheck(VkResult err) {
        if (err == VK_ERROR_DEVICE_LOST) {
            // deliberate policy: no in-process recovery, no restart — the
            // session dies with its reason on record
            sysE << "imway: vulkan device lost, exiting"_sv << endL;
            exit(1);
        }

        if (err < 0) {
            sysE << "imway: fatal: imgui vulkan call failed ("_sv << (long)err << ")"_sv << endL;
            abort();
        }
    }

    void surfaceColorCallback(const ImDrawList*, const ImDrawCmd* cmd) {
        Surface* surface = (Surface*)cmd->UserCallbackData;

        if (!surface) {
            ImGui_ImplVulkan_SetTextureColor(0, 0, 0, 0, 0, nullptr, nullptr, 0, 0, 0, 0, 0);

            return;
        }

        int source = surface->color.transfer == ColorTransfer::pq ? 4 : surface->color.transfer == ColorTransfer::hlg ? 5 : surface->color.transfer == ColorTransfer::extendedLinear ? 6 : surface->color.transfer == ColorTransfer::bt1886 ? 7 : surface->color.transfer == ColorTransfer::gamma22 ? 8 : surface->color.transfer == ColorTransfer::iccGamma ? 9 : 1;
        int primaries = surface->color.primaries == ColorPrimaries::bt2020 ? 1 : 0;
        float reference = source == 6 ? (float)surface->color.linearOneNits : source == 8 || source == 9 ? (float)surface->color.referenceNits : 0;

        ColorMatrix transform = colorPrimariesTransform(surface->color.primary, Chromaticities::bt2020());
        if (surface->color.directToBt2020) {
            for (int i = 0; i < 9; i++) {
                transform.v[i] = surface->color.toBt2020[i];
            }
        }
        float matrix[9];
        float gamma[3];

        for (int i = 0; i < 9; i++) {
            matrix[i] = (float)transform.v[i];
        }
        for (int i = 0; i < 3; i++) {
            gamma[i] = (float)surface->color.gamma[i];
        }

        int coefficients = 0;
        int range = 0;
        int chromaLocation = 0;
        int yuvBits = 0;

        if (surface->dmabuf && (surface->dmabuf->format == kFourccNv12 || surface->dmabuf->format == kFourccP010)) {
            coefficients = surface->representation.coefficients ? (int)surface->representation.coefficients : 2;
            range = surface->representation.range ? (int)surface->representation.range : 2;
            chromaLocation = surface->representation.chromaLocation ? (int)surface->representation.chromaLocation : 1;
            yuvBits = surface->dmabuf->format == kFourccP010 ? 10 : 8;
        }

        ImGui_ImplVulkan_SetTextureColor(source, primaries, reference, (float)surface->color.minNits, (float)surface->color.maxNits, matrix, gamma, (int)surface->representation.alphaMode, coefficients, range, chromaLocation, yuvBits);
    }

    void frameTimerCb(struct ev_loop*, ev_timer* w, int);
    void prepareCb(struct ev_loop*, ev_prepare* w, int);
    void clockTimerCb(struct ev_loop*, ev_timer* w, int);

    // the scene composes in linear half-float; the output pass maps it to
    // the scanout format
    constexpr VkFormat kSceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    struct RendererImpl;

    struct CallOutputResized: Listener {
        RendererImpl* parent;

        CallOutputResized(RendererImpl* p);
        void onListen(void*) override;
    };

    struct CallCaptureRetired: Listener {
        RendererImpl* parent;

        CallCaptureRetired(RendererImpl* p);
        void onListen(void* status) override;
    };

    struct CallScreenshotReady: Listener {
        RendererImpl* parent;

        CallScreenshotReady(RendererImpl* p);
        void onListen(void*) override;
    };

    struct RendererImpl: public Renderer, public IconResolver, public Listener, public FrameCapture {
        struct ev_loop* loop = nullptr;
        stl::ObjPool* pool = nullptr;
        Scene* scene = nullptr;
        ::Output* output = nullptr;
        int framesLimit = 0;
        int settleFrames = 0;

        int width = 0, height = 0;

        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice phys = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        u32 queueFamily = 0;
        VkQueue queue = VK_NULL_HANDLE;

        // Composition is always linear BT.2020 in absolute nits. target is the
        // final encoded image used only by non-scanout backends.
        VkImage sceneTarget = VK_NULL_HANDLE;
        VkDeviceMemory sceneMemory = VK_NULL_HANDLE;
        VkImageView sceneView = VK_NULL_HANDLE;
        VkFramebuffer sceneFramebuffer = VK_NULL_HANDLE;
        VkImage target = VK_NULL_HANDLE;
        VkDeviceMemory targetMemory = VK_NULL_HANDLE;
        VkImageView targetView = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass outputPass = VK_NULL_HANDLE;
        VkDescriptorSetLayout outputSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout outputPipeLayout = VK_NULL_HANDLE;
        VkPipeline outputPipeline = VK_NULL_HANDLE;
        VkDescriptorPool outputDescPool = VK_NULL_HANDLE;
        VkDescriptorSet outputDesc = VK_NULL_HANDLE;
        VkDescriptorSet cursorOutputDesc = VK_NULL_HANDLE;
        // cursor-plane encode pass: same fullscreen vertex stage, its own
        // fragment shader and push range (renderer_cursor.frag)
        VkPipelineLayout cursorPipeLayout = VK_NULL_HANDLE;
        VkPipeline cursorPipeline = VK_NULL_HANDLE;

        VkBuffer readback = VK_NULL_HANDLE;
        VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
        void* readbackMap = nullptr;

        // async screencopy readback: one GPU copy per frame serves every
        // registered consumer, the fence poll retires it; the loop never
        // waits the GPU for a capture
        struct CaptureWant {
            Listener* done = nullptr;
            int x = 0, y = 0, w = 0, h = 0;
        };

        Vector<CaptureWant> captureWants;
        VkCommandBuffer captureCmd = VK_NULL_HANDLE;
        VkFence captureFence = VK_NULL_HANDLE;
        VkBuffer captureBuf = VK_NULL_HANDLE;
        VkDeviceMemory captureMem = VK_NULL_HANDLE;
        void* captureMap = nullptr;
        int captureW = 0;
        int captureH = 0;
        int captureSeq = -1;
        FencePoll* captureFencePoll = nullptr;

        ScreenshotCapture* shotCapture = nullptr;
        bool shotRequested = false;

        VkCommandPool cmdPool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool frameInFlight = false;
        Vector<FrameResource*> inFlightFrames;
        VkSampler sampler = VK_NULL_HANDLE;

        // surface/icon texture descriptor sets are allocated by VkTexturePool
        // from our own growable pool chain, not imgui's single fixed pool, so
        // a client cannot exhaust it and crash us. imgui keeps its own small
        // pool only for its font atlas.
        VkTexturePool* texPool = nullptr;

        // color-management conversion compute pipeline
        SmallObjAllocator* alloc = nullptr;
        IntrusiveList textures;

        // icon textures keyed by Icon::gen; entries the previous frame did
        // not touch are destroyed at the start of the next one
        struct IconTex {
            u64 gen = 0;
            SurfaceTexture* tex = nullptr;
            bool used = false;
        };

        Vector<IconTex> iconTexes;

        struct DmabufCacheEntry {
            DmabufBuffer* key = nullptr;
            SurfaceTexture* tex = nullptr;
        };

        Vector<DmabufCacheEntry> dmabufCache;

        bool scanout = false;
        Vector<VkImageView> scanViews;
        Vector<VkFramebuffer> scanFbs;
        Vector<VkImage> scanImages;

        StringView fontPath;
        // the ui scale as last seen; the desktop owns changes and the
        // renderer rebakes cursor bitmaps when the scene scalar moves
        float uiScale = 1.f;
        ShadowSprite shadow; // window drop shadows, baked into the font atlas
        Composer* comp = nullptr;

        bool forceComposition = false;
        bool lastFrameDirect = false;
        // brightest possible scene value this frame (from visible surface
        // descriptions); the display tone map engages only above output peak
        double sceneMaxNits = 0;
        u8 pickR = 0, pickG = 0, pickB = 0;
        float frameMs[kFrameHistory] = {};
        int frameMsIdx = 0;

        // hardware cursor plane state
        bool hwCursorReady = false;
        int hwCapW = 0, hwCapH = 0;
        int hwHotX = 0, hwHotY = 0;
        bool hwVisible = false;
        int hwKind = -2;       // ImGuiMouseCursor of the uploaded image; -2 nothing, -3 client surface
        int pendingShape = -1; // shape waiting for end-of-frame rasterization
        // weak: identity of the uploaded cursor surface; a recycled
        // allocation must not read as "already uploaded"
        Weak<Surface> hwSurf;
        bool hwSurfStale = false;
        Vector<u32> hwShapeCache[ImGuiMouseCursor_COUNT];
        Vector<u32> hwScratch;

        // one-off offscreen rendering of cursor shapes
        VkImage curImg = VK_NULL_HANDLE;
        VkDeviceMemory curImgMem = VK_NULL_HANDLE;
        VkImageView curView = VK_NULL_HANDLE;
        VkFramebuffer curFb = VK_NULL_HANDLE;
        VkImage curScene = VK_NULL_HANDLE;
        VkDeviceMemory curSceneMem = VK_NULL_HANDLE;
        VkImageView curSceneView = VK_NULL_HANDLE;
        VkFramebuffer curSceneFb = VK_NULL_HANDLE;
        VkBuffer curReadback = VK_NULL_HANDLE;
        VkDeviceMemory curReadbackMem = VK_NULL_HANDLE;
        void* curReadbackMap = nullptr;
        VkCommandBuffer curCmd = VK_NULL_HANDLE;
        VkFence curFence = VK_NULL_HANDLE;

        int drmFd = -1;
        VkFormat fmt = kVkFormat;
        bool hasSyncFd = false;
        VkSemaphore syncOut = VK_NULL_HANDLE;
        bool syncOutStale = false;
        Vector<VkSemaphore> syncWaitPool;
        PFN_vkImportSemaphoreFdKHR importSemFd = nullptr;
        PFN_vkGetSemaphoreFdKHR getSemFd = nullptr;
        Vector<int> frameSyncFds;
        int presentFenceFd = -1;

        bool haveFrame = false;
        VkImage lastImage = VK_NULL_HANDLE;
        VkImageLayout lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        bool hasDmabuf = false;
        PFN_vkGetMemoryFdPropertiesKHR getMemoryFdProps = nullptr;

        RendererImpl(Composer& comp, const DeviceVk& vk, StringView font, float scale, int limit);

        ~RendererImpl() noexcept;

        void tick();

        u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags props);
        void createImage(int w, int h, VkFormat format, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem, u32 mips = 1);
        void createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf, VkDeviceMemory& mem, void** map);
        void setup();
        void applyOutputSize();
        void setupOutputTransform();
        void recordOutputTransform(VkCommandBuffer commands, VkFramebuffer outputFramebuffer, VkDescriptorSet source, int w, int h, const OutputColorState& color, double kelvin);
        void recordCursorTransform(VkCommandBuffer commands, VkFramebuffer outputFramebuffer, int w, int h);

        void drawSurfaceTree(Surface& s, float x, float y) override;
        void drawSurfaceTreeOverlay(Surface& s, float x, float y) override;
        void drawSurfaceRect(Surface& s, void* drawList, float x0, float y0, float x1, float y1) override;
        bool cursorPlane(int kind, Surface* cursorSurface, double x, double y, int hotX, int hotY) override;
        void cursorPlaneMove(double x, double y) override;
        void inspectorInfo(InspectorInfo& info) override;
        void rasterizeShape(int kind, u32* out);

        // IconResolver: gen -> texture, textures are born on first use
        u64 iconTexture(const Icon* icon) override;
        SurfaceTexture* makeIconTexture(const u32* argb, int w, int h);

        bool importDmabuf(Surface& s);
        void uploadSurface(Surface& s);
        void faultSurfaceOwner(Surface& s);
        void destroyTexture(SurfaceTexture* tex);
        SurfaceTexture* cacheFind(DmabufBuffer* b);
        bool cacheContainsTex(const SurfaceTexture* tex) const;
        void releaseSurfaceTexture(Surface& s);
        Surface* scanoutCandidate();
        bool surfaceVisible(Surface* s) const;
        bool finishGpuFrame(bool wait);

        bool wantFrame() const;

        void frameNow();
        void onListen(void* arg) override;
        bool renderFrame(int scanIdx);
        bool readbackLastFrame();
        bool screenshot(StringView path) override;
        bool captureSubmit(int x, int y, int w, int h, Listener& done) override;
        void captureCancel(Listener& done) override;
        bool captureRecord();
        void captureRetired(VkResult status);
        void captureDropAll();
        u64 colorIntermediateBytes() override;
        bool readPixel(int x, int y, u8& r, u8& g, u8& b) override;
        void captureScreenshot() override;
        void beginScreenshot();
        void syncScanoutTargets();
    };

    CallOutputResized::CallOutputResized(RendererImpl* p)
        : parent(p)
    {
    }

    void CallOutputResized::onListen(void*) {
        parent->applyOutputSize();
    }

    CallCaptureRetired::CallCaptureRetired(RendererImpl* p)
        : parent(p)
    {
    }

    void CallCaptureRetired::onListen(void* status) {
        parent->captureRetired(*(VkResult*)status);
    }

    CallScreenshotReady::CallScreenshotReady(RendererImpl* p)
        : parent(p)
    {
    }

    void CallScreenshotReady::onListen(void*) {
        parent->beginScreenshot();
    }

    void prepareCb(struct ev_loop*, ev_prepare* w, int) {
        auto* r = (RendererImpl*)w->data;

        if (r->wantFrame() && r->output->ready()) {
            r->frameNow();
        }
    }

    void frameTimerCb(struct ev_loop*, ev_timer* w, int) {
        ((RendererImpl*)w->data)->tick();
    }

    // the desktop renders on demand, wake it up so the clock stays fresh
    void clockTimerCb(struct ev_loop*, ev_timer* w, int) {
        ((RendererImpl*)w->data)->scene->needsFrame = true;
    }

}

TextureLease::TextureLease(void* o, SurfaceTexture* t, void (*d)(void*, SurfaceTexture*))
    : owner(o)
    , destroy(d)
{
    texture.bind(t->weak);
}

TextureLease::~TextureLease() noexcept {
    if (SurfaceTexture* t = texture.get()) {
        destroy(owner, t);
    }
}

void RenderContext::finish() {
    if (handled) {
        return;
    }

    VkClearValue clear{};

    clear.color = {{clearColor[0], clearColor[1], clearColor[2], clearColor[3]}};

    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};

    begin.renderPass = outputPass;
    begin.framebuffer = outputFramebuffer;
    begin.renderArea = {{0, 0}, {(u32)width, (u32)height}};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(drawData, commands);
    vkCmdEndRenderPass(commands);
    handled = true;
}

RendererImpl::RendererImpl(Composer& comp, const DeviceVk& vk, StringView font, float scale, int limit)
    : comp(&comp)
    , loop(comp.loop)
    , pool(comp.pool)
    , scene(comp.scene)
    , output(comp.output)
    , framesLimit(limit)
    , instance(vk.instance)
    , phys(vk.phys)
    , device(vk.device)
    , queueFamily(vk.queueFamily)
    , queue(vk.queue)
    , alloc(comp.alloc)
    , hasDmabuf(vk.hasDmabuf)
    , getMemoryFdProps(vk.getMemoryFdProps)
{
    fontPath = font;
    uiScale = scale;
    hasSyncFd = vk.hasSyncFd;
    drmFd = vk.drmFd;
    comp.iconResolver = this;
    comp.frameListeners.pushFront((Listener*)this);
    comp.frameCapture = (FrameCapture*)this;
    comp.outputResizedListeners.pushBack(comp.pool->make<CallOutputResized>(this));
    setup();
    // sized by the first mode announcement, like everything else here
    shotCapture = ScreenshotCapture::create(comp, vk, 0, 0, fmt, uiScale, *comp.pool->make<CallScreenshotReady>(this));

    if (output->vsynced()) {
        ev_prepare* prepare = createEvPrepare(*pool, loop);

        ev_prepare_init(prepare, prepareCb);
        prepare->data = this;
        ev_prepare_start(loop, prepare);
    } else {
        ev_timer* frameTimer = createEvTimer(*pool, loop);

        ev_timer_init(frameTimer, frameTimerCb, 0., 1.0 / scene->hz);
        frameTimer->data = this;
        ev_timer_start(loop, frameTimer);
    }

    ev_timer* clockTimer = createEvTimer(*pool, loop);

    ev_timer_init(clockTimer, clockTimerCb, 2., 2.);
    clockTimer->data = this;
    ev_timer_start(loop, clockTimer);
}

RendererImpl::~RendererImpl() noexcept {
    // the device must be idle before the pooled handles unwind (they die
    // right after this destructor); imgui and the churn-class resources
    // (textures, the recreatable syncOut, the present fence) are tied to
    // the impl and go here, everything setup-once sits in the pool
    vkDeviceWaitIdle(device);
    finishGpuFrame(false);

    if (presentFenceFd >= 0) {
        close(presentFenceFd);
        presentFenceFd = -1;
    }

    while (!textures.empty()) {
        destroyTexture((SurfaceTexture*)textures.mutBack());
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();

    if (syncOut) {
        vkDestroySemaphore(device, syncOut, nullptr);
    }

    for (VkFramebuffer fb : scanFbs) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }

    for (VkImageView view : scanViews) {
        vkDestroyImageView(device, view, nullptr);
    }
}

u64 RendererImpl::iconTexture(const Icon* icon) {
    if (!icon || icon->width <= 0 || icon->argb.length() < (size_t)icon->width * icon->height) {
        return 0;
    }

    for (size_t i = 0; i < iconTexes.length(); i++) {
        if (iconTexes[i].gen == icon->gen) {
            iconTexes.mut(i).used = true;

            return (u64)(uintptr_t)iconTexes[i].tex->ds;
        }
    }

    IconTex it;

    it.gen = icon->gen;
    it.tex = makeIconTexture(icon->argb.data(), icon->width, icon->height);
    it.used = true;
    iconTexes.pushBack(it);

    return (u64)(uintptr_t)it.tex->ds;
}

SurfaceTexture* RendererImpl::makeIconTexture(const u32* argb, int w, int h) {
    SurfaceTexture* tex = alloc->make<SurfaceTexture>();

    tex->weak.anchor(tex);
    tex->w = w;
    tex->h = h;

    // icons get drawn far below their raster size; without a mip chain the
    // minification aliases thin detail (icon borders) into stray specks
    for (int m = w > h ? w : h; m > 1; m /= 2) {
        tex->mips++;
    }

    createImage(w, h, kVkFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, tex->image, tex->memory, tex->mips);
    createHostBuffer((VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tex->staging, tex->stagingMemory, &tex->stagingMap);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    vci.image = tex->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = kVkFormat;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, tex->mips, 0, 1};
    VK_CHECK(vkCreateImageView(device, &vci, nullptr, &tex->view));
    tex->ds = texPool->alloc(tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, tex->dsPool);

    memcpy(tex->stagingMap, argb, (size_t)w * h * 4);
    tex->uploadRect = {0, 0, w, h};
    tex->needsUpload = true;
    textures.pushBack(tex);

    return tex;
}

bool RendererImpl::wantFrame() const {
    return scene->needsFrame || settleFrames > 0;
}

bool RendererImpl::surfaceVisible(Surface* s) const {
    if (!s || !s->contentMappedThroughAncestors()) {
        return false;
    }

    Surface* root = s->rootSurface();

    if (root->toplevel && root->toplevel->mapped && !root->toplevel->minimized) {
        return true;
    }

    for (Popup* popup : each<Popup>(scene->popups)) {
        if (popup->mapped && popup->surface.get() == root) {
            return true;
        }
    }

    return root == scene->cursorSurface || root == scene->dragIcon.get();
}

bool RendererImpl::finishGpuFrame(bool wait) {
    if (!frameInFlight) {
        return true;
    }

    VkResult status = wait ? vkWaitForFences(device, 1, &fence, VK_TRUE, kGpuWaitNs) : vkGetFenceStatus(device, fence);

    if (status == VK_NOT_READY) {
        return false;
    }

    if (status == VK_TIMEOUT) {
        // a frame fence that never signals is a hung gpu, not a slow one
        *(comp->log) << "imway: gpu hang (frame fence timeout), exiting"_sv << endL;
        exit(1);
    }

    if (status != VK_SUCCESS) {
        *(comp->log) << "imway: Vulkan frame fence failed ("_sv << (long)status << ")"_sv << endL;
        ev_break(loop, EVBREAK_ALL);

        return false;
    }

    status = vkResetFences(device, 1, &fence);

    if (status != VK_SUCCESS) {
        *(comp->log) << "imway: Vulkan frame fence reset failed ("_sv << (long)status << ")"_sv << endL;
        ev_break(loop, EVBREAK_ALL);

        return false;
    }

    frameInFlight = false;

    // the fence retired the frame, so a leftover signalled syncOut (failed
    // sync-fd export) can be replaced safely now
    if (syncOutStale) {
        syncOutStale = false;

        VkExportSemaphoreCreateInfo exp{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};

        exp.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        sci.pNext = &exp;
        vkDestroySemaphore(device, syncOut, nullptr);

        if (vkCreateSemaphore(device, &sci, nullptr, &syncOut) != VK_SUCCESS) {
            syncOut = VK_NULL_HANDLE;
            hasSyncFd = false;
        }
    }

    for (FrameResource* frame : inFlightFrames) {
        frameUnref(frame);
    }

    inFlightFrames.clear();

    return true;
}

void RendererImpl::onListen(void*) {
    finishGpuFrame(false);
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

void RendererImpl::createImage(int w, int h, VkFormat format, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem, u32 mips) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};

    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {(u32)w, (u32)h, 1};
    ici.mipLevels = mips;
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

void RendererImpl::setup() {
    scanout = output->scanoutCount() > 0;

    if (scanout) {
        fmt = output->scanoutBuffer(0)->format;
    }

    VkAttachmentDescription att{};

    att.format = kSceneFormat;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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
    VkSubpassDependency sceneDone{};

    sceneDone.srcSubpass = 0;
    sceneDone.dstSubpass = VK_SUBPASS_EXTERNAL;
    sceneDone.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    sceneDone.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    sceneDone.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    sceneDone.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &sceneDone;
    VK_CHECK(vkCreateRenderPass(device, &rpci, nullptr, &renderPass));

    att.format = fmt;
    att.finalLayout = scanout ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    rpci.dependencyCount = 0;
    rpci.pDependencies = nullptr;
    VK_CHECK(vkCreateRenderPass(device, &rpci, nullptr, &outputPass));

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};

    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = queueFamily;
    VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &cmdPool));

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cmd));
    VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &captureCmd));

    VkFenceCreateInfo fenci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

    VK_CHECK(vkCreateFence(device, &fenci, nullptr, &fence));
    VK_CHECK(vkCreateFence(device, &fenci, nullptr, &captureFence));
    captureFencePoll = FencePoll::create(*pool, loop, device, captureFence, *pool->make<CallCaptureRetired>(this));

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

        for (int i = 0; i < 16; i++) {
            VkSemaphore sem = VK_NULL_HANDLE;

            VK_CHECK(vkCreateSemaphore(device, &plain, nullptr, &sem));
            syncWaitPool.pushBack(sem);
        }
    }

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.maxLod = VK_LOD_CLAMP_NONE;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device, &sci, nullptr, &sampler));

    setupOutputTransform();

    texPool = VkTexturePool::create(*pool, device, sampler);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    applyImGuiTheme(ImGui::GetStyle(), comp->theme);

    ImGuiIO& io = ImGui::GetIO();

    StringView fontCandidates[] = {fontPath, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"_sv, "/usr/share/fonts/TTF/DejaVuSans.ttf"_sv};

    for (StringView f : fontCandidates) {
        if (f.empty()) {
            continue;
        }

        Buffer p(f);

        if (access(p.cStr(), R_OK) == 0 && io.Fonts->AddFontFromFileTTF(p.cStr(), 16.f, nullptr, io.Fonts->GetGlyphRangesCyrillic())) {
            break;
        }
    }

    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    // window drop shadows via the imway fork hook; the sprite lives in the
    // font atlas, repacks carry its pixels along
    bakeWindowShadow(io.Fonts, shadow);
    shadow.scale = uiScale;
    io.WindowShadowCallback = drawWindowShadow;
    io.WindowShadowCallbackUserData = &shadow;

    // clients are imgui windows: docking gives tabs and splits of wayland
    // windows for free
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // dragging inside the client area belongs to the client (text selection),
    // windows move by their title bar only
    io.ConfigWindowsMoveFromTitleBarOnly = true;

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
    // imgui's own pool now serves only its font atlas — surface/icon textures
    // come from VkTexturePool — so a small fixed size is plenty
    ii.DescriptorPoolSize = 64;
    ii.MinImageCount = 2;
    ii.ImageCount = 2;
    ii.CheckVkResultFn = imguiVkCheck;
    ii.PipelineInfoMain.RenderPass = renderPass;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.CustomShaderFragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ii.CustomShaderFragCreateInfo.codeSize = sizeof(renderer_scene_spv);
    ii.CustomShaderFragCreateInfo.pCode = renderer_scene_spv;

    STD_VERIFY(ImGui_ImplVulkan_Init(&ii));

    hwCapW = output->cursorCapW();
    hwCapH = output->cursorCapH();

    if (hwCapW > 0 && hwCapH > 0) {
        createImage(hwCapW, hwCapH, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, curScene, curSceneMem);
        createImage(hwCapW, hwCapH, fmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, curImg, curImgMem);

        VkImageViewCreateInfo cvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

        cvi.image = curScene;
        cvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        cvi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        cvi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &cvi, nullptr, &curSceneView));
        cvi.image = curImg;
        cvi.format = fmt;
        VK_CHECK(vkCreateImageView(device, &cvi, nullptr, &curView));

        VkFramebufferCreateInfo cfi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

        cfi.renderPass = renderPass;
        cfi.attachmentCount = 1;
        cfi.pAttachments = &curSceneView;
        cfi.width = (u32)hwCapW;
        cfi.height = (u32)hwCapH;
        cfi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &cfi, nullptr, &curSceneFb));
        cfi.renderPass = outputPass;
        cfi.pAttachments = &curView;
        VK_CHECK(vkCreateFramebuffer(device, &cfi, nullptr, &curFb));

        VkDescriptorImageInfo imageInfo{sampler, curSceneView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};

        write.dstSet = cursorOutputDesc;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

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

    // pool-owned for the renderer's whole life: the pool unwinds these
    // after ~RendererImpl has waited the device idle and shut imgui down,
    // and before DeviceVk dies. LIFO — dependents last. The sized targets
    // are guarded through lambdas reading the members, because a mode
    // announcement replaces the handles mid-life.
    pooledGuard(*pool, [this] {
        vkDestroyRenderPass(device, renderPass, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkFreeMemory(device, sceneMemory, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyImage(device, sceneTarget, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyImageView(device, sceneView, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyFramebuffer(device, sceneFramebuffer, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkFreeMemory(device, targetMemory, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyImage(device, target, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyImageView(device, targetView, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyRenderPass(device, outputPass, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyDescriptorSetLayout(device, outputSetLayout, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyPipelineLayout(device, outputPipeLayout, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyPipeline(device, outputPipeline, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyPipelineLayout(device, cursorPipeLayout, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyPipeline(device, cursorPipeline, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyDescriptorPool(device, outputDescPool, nullptr);
    });

    pooledGuard(*pool, [this] {
        vkFreeMemory(device, readbackMemory, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyBuffer(device, readback, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkFreeMemory(device, captureMem, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyBuffer(device, captureBuf, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyCommandPool(device, cmdPool, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyFence(device, fence, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyFence(device, captureFence, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroySampler(device, sampler, nullptr);
    });
    pooledGuard(*pool, [this] {
        for (VkSemaphore sem : syncWaitPool) {
            if (sem) {
                vkDestroySemaphore(device, sem, nullptr);
            }
        }
    });

    // the hardware-cursor objects are created at most once; destroying a
    // null handle is a no-op on the paths without a cursor plane
    pooledGuard(*pool, [this] {
        vkFreeMemory(device, curSceneMem, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyImage(device, curScene, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyImageView(device, curSceneView, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyFramebuffer(device, curSceneFb, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkFreeMemory(device, curImgMem, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyImage(device, curImg, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyImageView(device, curView, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyFramebuffer(device, curFb, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkFreeMemory(device, curReadbackMem, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyBuffer(device, curReadback, nullptr);
    });
    pooledGuard(*pool, [this] {
        vkDestroyFence(device, curFence, nullptr);
    });
}

// Every target sized by the output mode, built on the mode announcement:
// boot's first announcement builds them, a hotplug mode change rebuilds
// them — one path, so the resize side cannot silently rot. The backend
// idles the GPU before it rebuilds its scanout buffers and fires this.
void RendererImpl::applyOutputSize() {
    int w = scene->outW;
    int h = scene->outH;

    if (w <= 0 || h <= 0 || (w == width && h == height)) {
        return;
    }

    bool first = width == 0;

    if (!first) {
        finishGpuFrame(true);
        vkDestroyFramebuffer(device, sceneFramebuffer, nullptr);
        vkDestroyImageView(device, sceneView, nullptr);
        vkDestroyImage(device, sceneTarget, nullptr);
        vkFreeMemory(device, sceneMemory, nullptr);
        vkDestroyFramebuffer(device, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
        vkDestroyImageView(device, targetView, nullptr);
        vkDestroyImage(device, target, nullptr);
        vkFreeMemory(device, targetMemory, nullptr);
        target = VK_NULL_HANDLE;
        targetView = VK_NULL_HANDLE;
        targetMemory = VK_NULL_HANDLE;
        vkDestroyBuffer(device, readback, nullptr);
        vkFreeMemory(device, readbackMemory, nullptr);
        readback = VK_NULL_HANDLE;
        readbackMemory = VK_NULL_HANDLE;
        readbackMap = nullptr;
        captureDropAll();

        for (VkFramebuffer fb : scanFbs) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }

        for (VkImageView view : scanViews) {
            vkDestroyImageView(device, view, nullptr);
        }

        scanFbs.clear();
        scanViews.clear();
        scanImages.clear();
        haveFrame = false;
    }

    width = w;
    height = h;

    createImage(width, height, kSceneFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, sceneTarget, sceneMemory);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    vci.image = sceneTarget;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = kSceneFormat;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device, &vci, nullptr, &sceneView));

    if (!scanout) {
        createImage(width, height, fmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, target, targetMemory);

        vci.image = target;
        vci.format = fmt;
        VK_CHECK(vkCreateImageView(device, &vci, nullptr, &targetView));
    }

    createHostBuffer((VkDeviceSize)width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback, readbackMemory, &readbackMap);

    VkFramebufferCreateInfo sceneFci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

    sceneFci.renderPass = renderPass;
    sceneFci.attachmentCount = 1;
    sceneFci.pAttachments = &sceneView;
    sceneFci.width = width;
    sceneFci.height = height;
    sceneFci.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device, &sceneFci, nullptr, &sceneFramebuffer));

    if (scanout) {
        for (int i = 0; i < output->scanoutCount(); i++) {
            vci.image = output->scanoutBuffer(i)->image;
            vci.format = fmt;

            VkImageView view = VK_NULL_HANDLE;

            VK_CHECK(vkCreateImageView(device, &vci, nullptr, &view));
            scanImages.pushBack(vci.image);
            scanViews.pushBack(view);

            VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

            fci.renderPass = outputPass;
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

        fci.renderPass = outputPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &targetView;
        fci.width = width;
        fci.height = height;
        fci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &framebuffer));
    }

    // the output pass samples the scene through this set; the view is new
    VkDescriptorImageInfo imageInfo{sampler, sceneView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};

    write.dstSet = outputDesc;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    shotCapture->resize(width, height);
    (void)first;
    scene->needsFrame = true;
}

void RendererImpl::faultSurfaceOwner(Surface& s) {
    // a client-sized allocation failed: hand the owner to wayland for a
    // no_memory disconnect. An unowned surface (cursor, drag icon) just
    // stays untextured
    Surface* root = s.rootSurface();

    if (root && root->toplevel) {
        scene->renderFaults.pushBack(root->toplevel->id);
        scene->needsFrame = true;
    }
}

void RendererImpl::uploadSurface(Surface& s) {
    if (s.width <= 0 || s.height <= 0) {
        return;
    }

    SurfaceTexture* tex = s.texture.get();

    if (tex && tex->external) {
        releaseSurfaceTexture(s);
        tex = nullptr;
    }

    if (tex && (tex->w != s.width || tex->h != s.height)) {
        releaseSurfaceTexture(s);
        tex = nullptr;
    }

    bool fresh = tex == nullptr;

    if (!tex) {
        FrameResource* frame = frameCreate();

        tex = frame->make<SurfaceTexture>();
        tex->weak.anchor(tex);
        tex->arenaOwned = true;
        tex->w = s.width;
        tex->h = s.height;
        s.frame = frame;

        try {
            createImage(s.width, s.height, kVkFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, tex->image, tex->memory);
            createHostBuffer((VkDeviceSize)s.width * s.height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tex->staging, tex->stagingMemory, &tex->stagingMap);
        } catch (...) {
            *(comp->log) << "imway: texture allocation failed "_sv << s.width << "x"_sv << s.height << endL;
            s.frame = nullptr;
            frameUnref(frame);
            faultSurfaceOwner(s);

            return;
        }

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

        vci.image = tex->image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = kVkFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vci, nullptr, &tex->view);
        tex->ds = texPool->alloc(tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, tex->dsPool);

        if (!tex->ds) {
            // only genuine device OOM reaches here; drop the half-built texture
            // and leave the surface untextured (drawing skips it) this frame
            destroyTexture(tex);
            s.frame = nullptr;
            frameUnref(frame);

            return;
        }

        frame->make<TextureLease>(this, tex, [](void* owner, SurfaceTexture* texture) {
            ((RendererImpl*)owner)->destroyTexture(texture);
        });
        textures.pushBack(tex);
        s.texture.bind(tex->weak);
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
    SurfaceTexture* tex = s.texture.get();
    FrameResource* frame = s.frame;

    s.texture.reset();

    if (!tex) {
        return;
    }

    if (tex->external && cacheContainsTex(tex)) {
        return;
    }

    s.frame = nullptr;
    frameUnref(frame);
}

// a fullscreen client whose dmabuf can go straight to the plane, with no
// compositor chrome that would need composition over it
Surface* RendererImpl::scanoutCandidate() {
    // The dock and the top bar need no gate of their own: the candidate
    // below is a lone fullscreen toplevel, drawn output-sized at the origin
    // above the chrome, so the chrome is occluded whenever one exists. Any
    // open compositor ui (dialogs, overlays, toasts, the lock screen) is
    // one scene scalar, written by the desktop every frame.
    if (forceComposition || scene->overlayActive || !scene->popups.empty() || scene->dragIcon) {
        return nullptr;
    }

    // Night light is part of the composed output transform. A client buffer
    // cannot carry that adaptation, so direct scanout would visibly bypass it.
    if (output->colorTemp() > 0) {
        return nullptr;
    }

    if (scene->drawCursor) {
        Surface* cursor = scene->cursorSurface;

        if (!hwCursorReady || output->cursorCapW() <= 0 || (cursor && (cursor->dmabuf || cursor->width > output->cursorCapW() || cursor->height > output->cursorCapH()))) {
            return nullptr;
        }
    }

    Toplevel* fs = nullptr;
    int mapped = 0;

    forEach<Toplevel>(scene->toplevels, [&](Toplevel& t) {
        if (t.mapped && !t.minimized) {
            mapped++;

            if (t.fullscreen) {
                fs = &t;
            }
        }
    });

    if (!fs || mapped != 1) {
        return nullptr;
    }

    Surface* s = fs->surface.get();

    if (!s || !s->dmabuf || !s->hasContent || s->explicitSync || s->bufferTransform != 0 || s->bufferScale != 1 || s->bufferOffsetX != 0 || s->bufferOffsetY != 0 || s->vp.hasSrc || s->vp.hasDst || s->dmabuf->format == kFourccNv12 || s->dmabuf->format == kFourccP010 || s->representation.alphaMode != 0 || s->representation.coefficients || s->representation.chromaLocation) {
        return nullptr;
    }

    if (!s->stackBelow.empty() || !s->stackAbove.empty()) {
        return nullptr; // subsurfaces need composition
    }

    // a compositor-side opacity needs blending, which the primary plane
    // cannot do
    if (s->alphaMult < 1.f) {
        return nullptr;
    }

    // the primary plane does not blend: an alpha-capable format may bypass
    // composition only when the client declares the surface fully opaque
    if ((s->dmabuf->format == kFourccArgb || s->dmabuf->format == kFourccAr30 || s->dmabuf->format == kFourccAb30 || s->dmabuf->format == kFourccAb4h) && !s->opaqueCovers()) {
        return nullptr;
    }

    if (s->geomW() != scene->outW || s->geomH() != scene->outH) {
        return nullptr;
    }

    if (!directScanoutColorCompatible(output->colorState(), s->color)) {
        return nullptr;
    }

    return s;
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

    // nulls the lease's back-pointer and every Surface::texture aimed here
    tex->weak.invalidate();

    if (tex->ds) {
        texPool->free(tex->ds, tex->dsPool);
        tex->ds = VK_NULL_HANDLE;
        tex->dsPool = VK_NULL_HANDLE;
    }

    for (VkDeviceMemory m : tex->extraMemory) {
        if (m) {
            vkFreeMemory(device, m, nullptr);
        }
    }

    if (tex->view) {
        vkDestroyImageView(device, tex->view, nullptr);
    }

    if (tex->chromaView) {
        vkDestroyImageView(device, tex->chromaView, nullptr);
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

    tex->unlink();

    if (!tex->arenaOwned) {
        alloc->release(tex);
    }
}

void RendererImpl::setupOutputTransform() {
    VkDescriptorSetLayoutBinding binding{};

    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

    dlci.bindingCount = 1;
    dlci.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &dlci, nullptr, &outputSetLayout));

    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32 * sizeof(float)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

    plci.setLayoutCount = 1;
    plci.pSetLayouts = &outputSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &push;
    VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &outputPipeLayout));

    VkPushConstantRange cursorPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 12 * sizeof(float)};

    plci.pPushConstantRanges = &cursorPush;
    VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &cursorPipeLayout));

    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE, cursorFrag = VK_NULL_HANDLE;

    smci.codeSize = sizeof(fullscreen_spv);
    smci.pCode = fullscreen_spv;
    VK_CHECK(vkCreateShaderModule(device, &smci, nullptr, &vert));
    smci.codeSize = sizeof(renderer_output_spv);
    smci.pCode = renderer_output_spv;
    VK_CHECK(vkCreateShaderModule(device, &smci, nullptr, &frag));
    smci.codeSize = sizeof(renderer_cursor_spv);
    smci.pCode = renderer_cursor_spv;
    VK_CHECK(vkCreateShaderModule(device, &smci, nullptr, &cursorFrag));

    VkPipelineShaderStageCreateInfo stages[2] = {};

    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};

    input.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};

    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};

    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};

    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState attachment{};

    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};

    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;

    VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};

    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};

    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vertex;
    gpci.pInputAssemblyState = &input;
    gpci.pViewportState = &viewport;
    gpci.pRasterizationState = &raster;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &blend;
    gpci.pDynamicState = &dynamic;
    gpci.layout = outputPipeLayout;
    gpci.renderPass = outputPass;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &outputPipeline));

    stages[1].module = cursorFrag;
    gpci.layout = cursorPipeLayout;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &cursorPipeline));

    vkDestroyShaderModule(device, cursorFrag, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    vkDestroyShaderModule(device, vert, nullptr);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};

    dpci.maxSets = 2;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &outputDescPool));

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};

    dsai.descriptorPool = outputDescPool;
    VkDescriptorSet sets[2] = {};

    dsai.descriptorSetCount = 2;
    dsai.pSetLayouts = &outputSetLayout;
    VkDescriptorSetLayout layouts[2] = {outputSetLayout, outputSetLayout};

    dsai.pSetLayouts = layouts;
    VK_CHECK(vkAllocateDescriptorSets(device, &dsai, sets));
    outputDesc = sets[0];
    cursorOutputDesc = sets[1];
    // outputDesc gets its scene view on the first mode announcement, which
    // creates the sized scene target
}

void RendererImpl::recordOutputTransform(VkCommandBuffer commands, VkFramebuffer outputFramebuffer, VkDescriptorSet source, int w, int h, const OutputColorState& outputColor, double kelvin) {
    VkClearValue clear{};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};

    begin.renderPass = outputPass;
    begin.framebuffer = outputFramebuffer;
    begin.renderArea = {{0, 0}, {(u32)w, (u32)h}};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, outputPipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, outputPipeLayout, 0, 1, &source, 0, nullptr);

    VkViewport viewport{0.f, 0.f, (float)w, (float)h, 0.f, 1.f};
    VkRect2D scissor{{0, 0}, {(u32)w, (u32)h}};

    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);

    OutputMapping mapping = outputMapping(outputColor, kelvin);

    struct {
        float row[8][4];
    } push{};

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            push.row[row][col] = (float)mapping.toTarget.v[row * 3 + col];
            push.row[row + 3][col] = (float)mapping.fromTarget.v[row * 3 + col];
        }
    }

    // dither for the narrowest stage that quantizes: a 10-bit framebuffer
    // through an 8-bit link truncates at the connector, so the framebuffer
    // depth alone would make the noise sub-LSB
    u32 fbBits = fmt == VK_FORMAT_A2R10G10B10_UNORM_PACK32 ? 10u : 8u;
    u32 ditherBits = outputColor.bpc && outputColor.bpc < fbBits ? outputColor.bpc : fbBits;

    push.row[6][0] = (float)mapping.peakNits;
    push.row[6][1] = mapping.hdr ? 0.f : 203.f;
    push.row[6][2] = (float)((1u << ditherBits) - 1);
    // temporal dither: shift the noise pattern every frame so quantization
    // structure does not freeze in screen space. splitMix64 of the frame
    // index: a pure function of it, decorrelated between frames (a linear
    // walk repeats every cycle along one diagonal). Live displays only — a
    // headless screenshot must not depend on which frame it caught
    bool temporal = output->vsynced();

#ifdef IMWAY_FOR_TESTS
    // the dither-motion regression test forces the live behavior on headless
    temporal = temporal || getenv("IMWAY_TEMPORAL_DITHER");
#endif

    push.row[6][3] = (float)(splitMix64(temporal ? (u64)scene->framesDone : 0) % 4096);
    // the roll-off knee reshapes in-range content, so it only runs when
    // something visible can actually exceed the output peak
    push.row[7][0] = sceneMaxNits > mapping.peakNits * 1.0001 ? 1.f : 0.f;
    push.row[7][1] = (float)mapping.targetLuma.r;
    push.row[7][2] = (float)mapping.targetLuma.g;
    push.row[7][3] = (float)mapping.targetLuma.b;

    vkCmdPushConstants(commands, outputPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
}

void RendererImpl::recordCursorTransform(VkCommandBuffer commands, VkFramebuffer outputFramebuffer, int w, int h) {
    VkClearValue clear{};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};

    begin.renderPass = outputPass;
    begin.framebuffer = outputFramebuffer;
    begin.renderArea = {{0, 0}, {(u32)w, (u32)h}};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, cursorPipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, cursorPipeLayout, 0, 1, &cursorOutputDesc, 0, nullptr);

    VkViewport viewport{0.f, 0.f, (float)w, (float)h, 0.f, 1.f};
    VkRect2D scissor{{0, 0}, {(u32)w, (u32)h}};

    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);

    // the cursor scene is composed as SDR with unit white
    OutputMapping mapping = outputMapping(OutputColorState::sdr());
    float push[12] = {};

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            push[row * 4 + col] = (float)mapping.toTarget.v[row * 3 + col];
        }
    }

    vkCmdPushConstants(commands, cursorPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
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

    if (s.texture && s.texture.get() != cached) {
        releaseSurfaceTexture(s);
    }

    if (cached) {
        s.texture.bind(cached->weak);

        return true;
    }

    auto* tex = b->lifetime->make<SurfaceTexture>();

    tex->weak.anchor(tex);
    bool yuv = b->format == kFourccNv12 || b->format == kFourccP010;
    bool p010 = b->format == kFourccP010;
    VkFormat vkFormat = b->format == kFourccNv12 ? VK_FORMAT_G8_B8R8_2PLANE_420_UNORM : b->format == kFourccP010 ? VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16 : b->format == kFourccAr30 || b->format == kFourccXr30 ? VK_FORMAT_A2R10G10B10_UNORM_PACK32 : b->format == kFourccAb30 || b->format == kFourccXb30 ? VK_FORMAT_A2B10G10R10_UNORM_PACK32 : b->format == kFourccAb4h || b->format == kFourccXb4h ? VK_FORMAT_R16G16B16A16_SFLOAT : kVkFormat;

    tex->w = b->width;
    tex->h = b->height;
    tex->external = true;
    tex->arenaOwned = true;
    VkSubresourceLayout planes[kDmabufMaxPlanes] = {};
    bool disjoint = false;
    struct stat firstStat{};
    bool firstStatOk = fstat(b->fds[0], &firstStat) == 0;

    for (int i = 0; i < b->nplanes; i++) {
        planes[i].offset = b->offsets[i];
        planes[i].rowPitch = b->strides[i];

        struct stat planeStat{};

        if (i > 0 && (!firstStatOk || fstat(b->fds[i], &planeStat) != 0 || planeStat.st_dev != firstStat.st_dev || planeStat.st_ino != firstStat.st_ino)) {
            disjoint = true;
        }
    }

    VkImageDrmFormatModifierExplicitCreateInfoEXT modInfo{VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};

    modInfo.drmFormatModifier = b->modifier;
    modInfo.drmFormatModifierPlaneCount = (u32)b->nplanes;
    modInfo.pPlaneLayouts = planes;

    VkExternalMemoryImageCreateInfo extInfo{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};

    extInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    extInfo.pNext = &modInfo;

    VkFormat planeFormats[2] = {
        p010 ? VK_FORMAT_R16_UNORM : VK_FORMAT_R8_UNORM,
        p010 ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R8G8_UNORM,
    };
    VkImageFormatListCreateInfo formatList{VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};

    if (yuv) {
        formatList.viewFormatCount = 2;
        formatList.pViewFormats = planeFormats;
        formatList.pNext = extInfo.pNext;
        extInfo.pNext = &formatList;
    }

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};

    ici.pNext = &extInfo;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = vkFormat;
    ici.extent = {(u32)b->width, (u32)b->height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (yuv) {
        ici.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    }

    if (disjoint) {
        ici.flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
    }

    if (vkCreateImage(device, &ici, nullptr, &tex->image) != VK_SUCCESS) {
        *(comp->log) << "imway: dmabuf vkCreateImage failed"_sv << endL;
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
        bool allocated = memType != UINT32_MAX && vkAllocateMemory(device, &mai, nullptr, &tex->memory) == VK_SUCCESS;

        // a successful import hands fd ownership to Vulkan: closing it after
        // a failed bind would double-close once vkFreeMemory drops it
        if (!allocated) {
            close(fd);
        }

        bound = allocated && vkBindImageMemory(device, tex->image, tex->memory, 0) == VK_SUCCESS;
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
        *(comp->log) << "imway: dmabuf memory import failed ("_sv << b->nplanes << " planes)"_sv << endL;
        vkDestroyImage(device, tex->image, nullptr);

        if (tex->memory) {
            vkFreeMemory(device, tex->memory, nullptr);
        }

        for (VkDeviceMemory m : tex->extraMemory) {
            if (m) {
                vkFreeMemory(device, m, nullptr);
            }
        }

        return false;
    }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    vci.image = tex->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = yuv ? planeFormats[0] : vkFormat;
    vci.subresourceRange = {yuv ? VK_IMAGE_ASPECT_PLANE_0_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    if (b->format == kFourccXrgb || b->format == kFourccXr30 || b->format == kFourccXb30 || b->format == kFourccXb4h) {
        vci.components.a = VK_COMPONENT_SWIZZLE_ONE;
    }

    if (vkCreateImageView(device, &vci, nullptr, &tex->view) != VK_SUCCESS) {
        *(comp->log) << "imway: dmabuf image view failed"_sv << endL;
        destroyTexture(tex);

        return false;
    }

    if (yuv) {
        vci.format = planeFormats[1];
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;

        if (vkCreateImageView(device, &vci, nullptr, &tex->chromaView) != VK_SUCCESS) {
            *(comp->log) << "imway: dmabuf chroma view failed"_sv << endL;
            destroyTexture(tex);

            return false;
        }
    }

    tex->ds = texPool->alloc(tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, tex->dsPool, tex->chromaView);

    if (!tex->ds) {
        // genuine device OOM: drop this import, the surface stays untextured
        destroyTexture(tex);

        return false;
    }

    b->lifetime->make<TextureLease>(this, tex, [](void* owner, SurfaceTexture* texture) {
        ((RendererImpl*)owner)->destroyTexture(texture);
    });
    textures.pushBack(tex);
    dmabufCache.pushBack({b, tex});
    s.texture.bind(tex->weak);

    return true;
}

ImVec2 transformedUv(int transform, float x, float y) {
    switch (transform) {
        case 1:
            return ImVec2(1.f - y, x); // 90
        case 2:
            return ImVec2(1.f - x, 1.f - y); // 180
        case 3:
            return ImVec2(y, 1.f - x); // 270
        case 4:
            return ImVec2(1.f - x, y); // flipped
        case 5:
            return ImVec2(1.f - y, 1.f - x); // flipped 90
        case 6:
            return ImVec2(x, 1.f - y); // flipped 180
        case 7:
            return ImVec2(y, x); // flipped 270
        default:
            return ImVec2(x, y);
    }
}

void surfaceUvs(const Surface& s, float x0, float y0, float x1, float y1, ImVec2 (&uv)[4]) {
    uv[0] = transformedUv(s.bufferTransform, x0, y0);
    uv[1] = transformedUv(s.bufferTransform, x1, y0);
    uv[2] = transformedUv(s.bufferTransform, x1, y1);
    uv[3] = transformedUv(s.bufferTransform, x0, y1);
}

// x/y is where the VISIBLE part (window geometry) goes; imgX/imgY keep the
// screen position of the surface origin, so client coordinate math holds
void RendererImpl::drawSurfaceTree(Surface& s, float x, float y) {
    float gx = 0.f, gy = 0.f;
    float w = (float)s.viewW(), h = (float)s.viewH();
    float ux0 = 0.f, uy0 = 0.f, ux1 = 1.f, uy1 = 1.f;
    bool viewported = s.vp.hasSrc || s.vp.hasDst;
    bool swapped = s.bufferTransform == 1 || s.bufferTransform == 3 || s.bufferTransform == 5 || s.bufferTransform == 7;
    float tw = (float)(swapped ? s.height : s.width), th = (float)(swapped ? s.width : s.height);

    x += (float)s.bufferOffsetX;
    y += (float)s.bufferOffsetY;

    if (s.texture && s.vp.hasSrc && tw > 0 && th > 0) {
        ux0 = (float)(s.vp.sx * s.bufferScale / tw);
        uy0 = (float)(s.vp.sy * s.bufferScale / th);
        ux1 = (float)((s.vp.sx + s.vp.sw) * s.bufferScale / tw);
        uy1 = (float)((s.vp.sy + s.vp.sh) * s.bufferScale / th);
    } else if (s.texture && !viewported && s.hasGeom && tw > 0 && th > 0) {
        gx = (float)s.geomX();
        gy = (float)s.geomY();
        w = (float)s.geomW();
        h = (float)s.geomH();
        ux0 = gx * s.bufferScale / tw;
        uy0 = gy * s.bufferScale / th;
        ux1 = (gx + w) * s.bufferScale / tw;
        uy1 = (gy + h) * s.bufferScale / th;
    }

    forEach<Subsurface>(s.stackBelow, [&](Subsurface& c) {
        if (c.surface && c.surface->hasContent) {
            drawSurfaceTree(*c.surface, x - gx + (float)c.x, y - gy + (float)c.y);
        }
    });

    if (s.texture) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImVec2 uv[4];

        surfaceUvs(s, ux0, uy0, ux1, uy1, uv);

        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddCallback(surfaceColorCallback, &s);

        ImU32 tint = IM_COL32(255, 255, 255, (int)(s.alphaMult * 255.f + .5f));

        if (s.bufferTransform == 0) {
            ImGui::ImageWithBg((ImTextureID)(uintptr_t)s.texture->ds, ImVec2(w, h), uv[0], uv[2], ImVec4(0.f, 0.f, 0.f, 0.f), ImVec4(1.f, 1.f, 1.f, s.alphaMult));
        } else {
            ImGui::PushID(&s);
            ImGui::InvisibleButton("surface", ImVec2(w, h));
            draw->AddImageQuad((ImTextureID)(uintptr_t)s.texture->ds, ImVec2(x, y), ImVec2(x + w, y), ImVec2(x + w, y + h), ImVec2(x, y + h), uv[0], uv[1], uv[2], uv[3], tint);
            ImGui::PopID();
        }

        draw->AddCallback(surfaceColorCallback, nullptr);

        s.imgX = x - gx;
        s.imgY = y - gy;
        // AllowWhenBlockedByActiveItem: during a held drag imgui parks
        // ActiveId on the pressed window's MoveId and plain hover then fails
        // for every OTHER window — dnd could never re-target across windows
        s.hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    }

    forEach<Subsurface>(s.stackAbove, [&](Subsurface& c) {
        if (c.surface && c.surface->hasContent) {
            drawSurfaceTree(*c.surface, x - gx + (float)c.x, y - gy + (float)c.y);
        }
    });
}

void RendererImpl::drawSurfaceTreeOverlay(Surface& s, float x, float y) {
    float gx = 0.f, gy = 0.f;
    float w = (float)s.viewW(), h = (float)s.viewH();
    float ux0 = 0.f, uy0 = 0.f, ux1 = 1.f, uy1 = 1.f;
    bool viewported = s.vp.hasSrc || s.vp.hasDst;
    bool swapped = s.bufferTransform == 1 || s.bufferTransform == 3 || s.bufferTransform == 5 || s.bufferTransform == 7;
    float tw = (float)(swapped ? s.height : s.width), th = (float)(swapped ? s.width : s.height);

    x += (float)s.bufferOffsetX;
    y += (float)s.bufferOffsetY;

    if (s.texture && s.vp.hasSrc && tw > 0 && th > 0) {
        ux0 = (float)(s.vp.sx * s.bufferScale / tw);
        uy0 = (float)(s.vp.sy * s.bufferScale / th);
        ux1 = (float)((s.vp.sx + s.vp.sw) * s.bufferScale / tw);
        uy1 = (float)((s.vp.sy + s.vp.sh) * s.bufferScale / th);
    } else if (s.texture && !viewported && s.hasGeom && tw > 0 && th > 0) {
        gx = (float)s.geomX();
        gy = (float)s.geomY();
        w = (float)s.geomW();
        h = (float)s.geomH();
        ux0 = gx * s.bufferScale / tw;
        uy0 = gy * s.bufferScale / th;
        ux1 = (gx + w) * s.bufferScale / tw;
        uy1 = (gy + h) * s.bufferScale / th;
    }

    forEach<Subsurface>(s.stackBelow, [&](Subsurface& c) {
        if (c.surface && c.surface->hasContent) {
            drawSurfaceTreeOverlay(*c.surface, x - gx + (float)c.x, y - gy + (float)c.y);
        }
    });

    if (s.texture) {
        ImVec2 uv[4];

        surfaceUvs(s, ux0, uy0, ux1, uy1, uv);
        ImDrawList* draw = ImGui::GetForegroundDrawList();

        draw->AddCallback(surfaceColorCallback, &s);
        draw->AddImageQuad((ImTextureID)(uintptr_t)s.texture->ds, ImVec2(x, y), ImVec2(x + w, y), ImVec2(x + w, y + h), ImVec2(x, y + h), uv[0], uv[1], uv[2], uv[3], IM_COL32(255, 255, 255, (int)(s.alphaMult * 255.f + .5f)));
        draw->AddCallback(surfaceColorCallback, nullptr);
        s.imgX = x - gx;
        s.imgY = y - gy;

        ImVec2 m = ImGui::GetIO().MousePos;

        s.hovered = m.x >= x && m.y >= y && m.x < x + w && m.y < y + h;
    }

    forEach<Subsurface>(s.stackAbove, [&](Subsurface& c) {
        if (c.surface && c.surface->hasContent) {
            drawSurfaceTreeOverlay(*c.surface, x - gx + (float)c.x, y - gy + (float)c.y);
        }
    });
}

void RendererImpl::drawSurfaceRect(Surface& s, void* drawList, float x0, float y0, float x1, float y1) {
    SurfaceTexture* tex = s.texture.get();

    if (!tex) {
        return;
    }

    auto* dl = (ImDrawList*)drawList;
    float sw = (float)s.geomW(), sh = (float)s.geomH();
    float texW = (float)tex->w, texH = (float)tex->h;
    ImVec2 uv0((float)s.geomX() / texW, (float)s.geomY() / texH);
    ImVec2 uv1(((float)s.geomX() + sw) / texW, ((float)s.geomY() + sh) / texH);

    dl->AddCallback(surfaceColorCallback, &s);
    dl->AddImage((ImTextureID)(uintptr_t)tex->ds, ImVec2(x0, y0), ImVec2(x1, y1), uv0, uv1);
    dl->AddCallback(surfaceColorCallback, nullptr);
}

// The cursor plane driver: the desktop hands over the wanted content and
// position each frame; true means the plane presents the cursor and
// nothing needs composing. Shape bitmaps rasterize lazily at the next
// frame edge and cache per kind.
bool RendererImpl::cursorPlane(int kind, Surface* cs, double x, double y, int hotX, int hotY) {
    // the plane can get rejected at runtime (mode-dependent), re-check live
    bool hwCursor = hwCursorReady && output->cursorCapW() > 0;

    if (cs) {
        // client-provided cursor surface: feed its pixels to the cursor plane.
        // The plane copies raw buffer pixels, so a scaled/transformed/viewport
        // cursor (HiDPI themes) must fall back to composition or it shows up
        // double-size with a misplaced hotspot; likewise anything that needs
        // the color pipeline (managed description, non-default alpha)
        bool plainBuffer = cs->bufferScale == 1 && cs->bufferTransform == 0 && !cs->vp.hasSrc && !cs->vp.hasDst;
        bool plainColor = !cs->color.managed() && cs->representation.alphaMode == 0 && !cs->representation.coefficients;
        bool hwOk = hwCursor && !cs->dmabuf && plainBuffer && plainColor && cs->width > 0 && cs->height > 0 && cs->width <= hwCapW && cs->height <= hwCapH && cs->pixels.length() >= (size_t)cs->width * cs->height * 4;

        if (!hwOk) {
            if (hwCursor) {
                hwVisible = false;
                output->setCursorPos(0, 0, false);
            }

            return false;
        }

        if (hwSurf.get() != cs || hwSurfStale) {
            hwScratch.zero((size_t)hwCapW * hwCapH);

            for (int sy = 0; sy < cs->height; sy++) {
                memcpy(hwScratch.mutData() + (size_t)sy * hwCapW, (const u32*)cs->pixels.data() + (size_t)sy * cs->width, (size_t)cs->width * 4);
            }

            output->setCursorImage(hwScratch.data());
            hwSurf.bind(cs->weak);
            hwKind = -3;
            hwSurfStale = false;
        }

        hwHotX = hotX;
        hwHotY = hotY;
        hwVisible = true;
        output->setCursorPos((int)x - hwHotX, (int)y - hwHotY, true);

        return true;
    }

    if (!hwCursor) {
        hwVisible = false;

        return false;
    }

    if (kind == ImGuiMouseCursor_None) {
        hwVisible = false;
        output->setCursorPos(0, 0, false);

        return true;
    }

    if (kind < 0 || kind >= ImGuiMouseCursor_COUNT) {
        kind = ImGuiMouseCursor_Arrow;
    }

    if (hwKind != kind) {
        Vector<u32>& img = hwShapeCache[kind];

        if (!img.length()) {
            // rasterizing goes through the imgui vulkan backend, which must
            // not run mid-frame (on the very first frame it is not even
            // initialized yet and produces an empty image): defer to the
            // start of the next frame, when the queue has drained
            pendingShape = kind;
            scene->needsFrame = true;
        } else {
            output->setCursorImage(img.data());
            hwKind = kind;
            hwSurf.reset();
        }
    }

    hwHotX = hwCapW / 2;
    hwHotY = hwCapH / 2;
    hwVisible = true;
    output->setCursorPos((int)x - hwHotX, (int)y - hwHotY, true);

    return true;
}

// input-rate plane moves: the cursor tracks the pointer between frames
void RendererImpl::cursorPlaneMove(double x, double y) {
    if (hwCursorReady && hwVisible) {
        output->setCursorPos((int)x - hwHotX, (int)y - hwHotY, true);
    }
}

void RendererImpl::inspectorInfo(InspectorInfo& info) {
    info.frameMs = frameMs;
    info.frameIdx = frameMsIdx;
    info.textures = textures.length();
    info.dmabufCache = dmabufCache.length();
    info.hwCursorKind = hwKind;
    info.hwCursorVisible = hwVisible;
}

// one-off offscreen render of a cursor shape into a premultiplied ARGB image
// for the KMS cursor plane; the hotspot lands at the canvas center
void RendererImpl::rasterizeShape(int kind, u32* out) {
    ImDrawList dl(ImGui::GetDrawListSharedData());

    dl._ResetForNewFrame();
    dl.PushTexture(ImGui::GetIO().Fonts->TexRef);
    dl.PushClipRect(ImVec2(0.f, 0.f), ImVec2((float)hwCapW, (float)hwCapH), false);

    float s = uiScale;
    float half = (float)(hwCapW < hwCapH ? hwCapW : hwCapH) * 0.5f;

    // the plane cannot scale: clamp so the largest shape fits the buffer
    if (21.f * s > half - 2.f) {
        s = (half - 2.f) / 21.f;
    }

    comp->desktop->drawCursorShape(&dl, ImVec2((float)hwCapW * 0.5f, (float)hwCapH * 0.5f), s, kind);
    dl.PopClipRect();
    dl.PopTexture();

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
    // the docking backend dereferences the owner viewport for its render
    // buffers; the hand-built draw data must point at the main one
    dd.OwnerViewport = ImGui::GetMainViewport();

    // the imgui backend cycles shared vertex buffers; the caller runs this
    // at the start of a frame, after the previous frame's fence retired, so
    // the queue is already empty and this wait is a cheap guard
    vkQueueWaitIdle(queue);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(curCmd, 0);
    vkBeginCommandBuffer(curCmd, &bi);

    VkClearValue clear{};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};

    rbi.renderPass = renderPass;
    rbi.framebuffer = curSceneFb;
    rbi.renderArea = {{0, 0}, {(u32)hwCapW, (u32)hwCapH}};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(curCmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_SetSdrWhite(1.f);
    ImGui_ImplVulkan_RenderDrawData(&dd, curCmd);
    vkCmdEndRenderPass(curCmd);

    recordCursorTransform(curCmd, curFb, hwCapW, hwCapH);

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

    if (VkResult res = vkQueueSubmit(queue, 1, &si, curFence); res != VK_SUCCESS) {
        // out is pre-zeroed by the caller: a transparent cursor beats
        // freezing the whole session in an infinite fence wait
        *(comp->log) << "imway: cursor rasterize submit failed ("_sv << (long)res << ")"_sv << endL;

        return;
    }

    vkWaitOrDie(device, curFence, "cursor rasterize");
    vkResetFences(device, 1, &curFence);

    // B8G8R8A8 bytes match DRM ARGB8888, and rendering onto transparent
    // black with src-alpha blending yields premultiplied pixels
    memcpy(out, curReadbackMap, (size_t)hwCapW * hwCapH * 4);

    if (fmt == VK_FORMAT_A2R10G10B10_UNORM_PACK32) {
        // HDR scanout renders 10-bit; the cursor plane wants ARGB8888
        // (alpha collapses to 4 levels, tolerable on a 1px fringe)
        for (size_t i = 0; i < (size_t)hwCapW * hwCapH; i++) {
            u32 v = out[i];
            u32 a = ((v >> 30) & 3) * 85;
            u32 r = (v >> 22) & 0xff;
            u32 g = (v >> 12) & 0xff;
            u32 b = (v >> 2) & 0xff;

            out[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

void RendererImpl::syncScanoutTargets() {
    for (int i = 0; i < output->scanoutCount(); i++) {
        VkImage image = output->scanoutBuffer(i)->image;

        if (scanImages[i] == image) {
            continue;
        }

        vkDestroyFramebuffer(device, scanFbs[i], nullptr);
        vkDestroyImageView(device, scanViews[i], nullptr);

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

        vci.image = image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &vci, nullptr, &scanViews.mut(i)));

        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};

        fci.renderPass = outputPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &scanViews[i];
        fci.width = width;
        fci.height = height;
        fci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &scanFbs.mut(i)));
        scanImages.mut(i) = image;
    }
}

bool RendererImpl::renderFrame(int scanIdx) {
    if (scanIdx >= 0) {
        syncScanoutTargets();
    }

    Vector<SurfaceTexture*> externalFirstUses;
    Vector<VkSemaphore> waits;
    Vector<VkPipelineStageFlags> waitStages;
    Vector<Surface*> explicitSurfaces;

    frameSyncFds.clear();

    if (presentFenceFd >= 0) {
        // presentImage consumes the fence synchronously.  Reaching another
        // frame with one still pending would otherwise leak it.
        close(presentFenceFd);
        presentFenceFd = -1;
    }

    auto waitOnSyncFile = [&](int syncFd) {
        size_t index = waits.length();

        if (index == syncWaitPool.length()) {
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore sem = VK_NULL_HANDLE;

            if (vkCreateSemaphore(device, &sci, nullptr, &sem) != VK_SUCCESS) {
                close(syncFd);

                return false;
            }

            syncWaitPool.pushBack(sem);
        }

        VkImportSemaphoreFdInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};

        imp.semaphore = syncWaitPool[index];
        imp.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
        imp.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        imp.fd = syncFd;

        if (importSemFd(device, &imp) != VK_SUCCESS) {
            close(syncFd);

            return false;
        }

        waits.pushBack(syncWaitPool[index]);
        waitStages.pushBack(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        return true;
    };

    if (hasSyncFd) {
        forEach<Surface, SceneNode>(scene->surfaces, [&](Surface& value) {
            Surface* s = &value;

            if (!surfaceVisible(s) || !s->dmabuf || !s->texture || !s->texture->external) {
                return;
            }

            if (s->explicitSync) {
                if (s->syncAcquireWait) {
                    // commits park in wayland.cpp until the acquire point
                    // materializes, so a real fence is normally here and the
                    // GPU waits on it; without eventfd support an
                    // unmaterialized point can still arrive — sample
                    // unsynchronized rather than stall the frame loop
                    u32 handle = s->syncAcquireHandle;
                    u64 point = s->syncAcquirePoint;
                    bool ready = drmSyncobjTimelineWait(drmFd, &handle, &point, 1, 0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE, nullptr) == 0;
                    u32 binary = 0;
                    int syncFd = -1;
                    bool exported = ready && drmSyncobjCreate(drmFd, 0, &binary) == 0 && drmSyncobjTransfer(drmFd, binary, 0, s->syncAcquireHandle, s->syncAcquirePoint, 0) == 0 && drmSyncobjExportSyncFile(drmFd, binary, &syncFd) == 0 && syncFd >= 0;

                    if (binary) {
                        drmSyncobjDestroy(drmFd, binary);
                    }

                    if (!exported && syncFd >= 0) {
                        close(syncFd);
                        syncFd = -1;
                    }

                    if (!exported || !waitOnSyncFile(syncFd)) {
                        *(comp->log) << "imway: acquire point unavailable, sampling unsynchronized"_sv << endL;
                    }

                    explicitSurfaces.pushBack(s);
                }

                return;
            }

            for (int i = 0; i < s->dmabuf->nplanes; i++) {
                int fd = s->dmabuf->fds[i];

                if (fd < 0 || contains(frameSyncFds, fd)) {
                    continue;
                }

                frameSyncFds.pushBack(fd);

                dma_buf_export_sync_file exp{};

                exp.flags = DMA_BUF_SYNC_WRITE;
                exp.fd = -1;

                if (ioctl(fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exp) != 0 || exp.fd < 0) {
                    continue;
                }

                waitOnSyncFile(exp.fd);
            }
        });
    }

    ImGuiIO& frameIo = ImGui::GetIO();

    frameIo.DisplaySize = ImVec2((float)width, (float)height);
    frameIo.DeltaTime = (float)(1.0 / scene->hz);

    // the desktop applied a scale change: rebake the cursor bitmaps
    if (scene->uiScale != uiScale) {
        uiScale = scene->uiScale;
        shadow.scale = uiScale;

        for (Vector<u32>& img : hwShapeCache) {
            img.clear();
        }

        if (hwKind >= 0) {
            pendingShape = hwKind;
            hwKind = -2;
        }
    }

    // icon textures the previous frame did not reference die now
    for (size_t i = 0; i < iconTexes.length();) {
        if (!iconTexes[i].used) {
            // destroyTexture unlinks from textures and releases the object
            destroyTexture(iconTexes[i].tex);
            iconTexes.mut(i) = iconTexes.back();
            iconTexes.popBack();
        } else {
            iconTexes.mut(i).used = false;
            i++;
        }
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    comp->desktop->build();
    ImGui::Render();

    // ImGui deliberately trickles press/release and text events across frames.
    // Keep the on-demand renderer running until that queue is empty; otherwise
    // an input burst can strand half of a password until some later event.
    if (GImGui->InputEventsQueue.Size) {
        scene->needsFrame = true;
    }

    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    forEach<SurfaceTexture>(textures, [&](SurfaceTexture& value) {
        SurfaceTexture* tex = &value;

        if (!tex->external || !tex->firstUse) {
            return;
        }

        VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = tex->image;
        toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
        externalFirstUses.pushBack(tex);
    });

    forEach<SurfaceTexture>(textures, [&](SurfaceTexture& value) {
        SurfaceTexture* tex = &value;

        if (!tex->needsUpload) {
            return;
        }

        VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

        toDst.srcAccessMask = tex->firstUse ? 0 : VK_ACCESS_SHADER_READ_BIT;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toDst.oldLayout = tex->firstUse ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = tex->image;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, tex->mips, 0, 1};
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

        if (tex->mips > 1) {
            // rebuild the mip chain from level 0
            int mw = tex->w, mh = tex->h;

            for (u32 i = 1; i < tex->mips; i++) {
                VkImageMemoryBarrier srcB = toDst;

                srcB.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                srcB.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                srcB.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                srcB.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                srcB.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1};
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &srcB);

                int nw = mw > 1 ? mw / 2 : 1;
                int nh = mh > 1 ? mh / 2 : 1;
                VkImageBlit blit{};

                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
                blit.srcOffsets[1] = {mw, mh, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
                blit.dstOffsets[1] = {nw, nh, 1};
                vkCmdBlitImage(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                mw = nw;
                mh = nh;
            }

            // levels 0..n-2 sit in TRANSFER_SRC, the last one in DST
            VkImageMemoryBarrier toRead = toDst;

            toRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, tex->mips - 1, 0, 1};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
            toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, tex->mips - 1, 1, 0, 1};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
        } else {
            VkImageMemoryBarrier toRead = toDst;

            toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
        }

        tex->needsUpload = false;
        tex->firstUse = false;
    });

    const OutputColorState& outputColor = output->colorState();

    ImGui_ImplVulkan_SetSdrWhite(outputColor.hdr() ? (float)outputColor.sdrWhiteNits : 203.f);

    RenderContext renderCtx;

    renderCtx.physicalDevice = phys;
    renderCtx.device = device;
    renderCtx.commands = cmd;
    renderCtx.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    renderCtx.sampler = sampler;
    renderCtx.outputPass = renderPass;
    renderCtx.outputFramebuffer = sceneFramebuffer;
    renderCtx.textures = texPool;
    renderCtx.drawData = ImGui::GetDrawData();
    auto srgbToLinear = [](float x) {
        return x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f);
    };
    const ThemeColor& desktopColor = comp->theme.desktop;
    float r = srgbToLinear(desktopColor.r);
    float g = srgbToLinear(desktopColor.g);
    float b = srgbToLinear(desktopColor.b);
    float white = outputColor.hdr() ? (float)outputColor.sdrWhiteNits : 203.f;

    renderCtx.clearColor[0] = (0.627404f * r + 0.329283f * g + 0.043313f * b) * white;
    renderCtx.clearColor[1] = (0.069097f * r + 0.919540f * g + 0.011362f * b) * white;
    renderCtx.clearColor[2] = (0.016391f * r + 0.088013f * g + 0.895595f * b) * white;
    renderCtx.clearColor[3] = desktopColor.a;
    renderCtx.width = width;
    renderCtx.height = height;

    forEach<Filter>(comp->filters, [&](Filter& filter) {
        filter.apply(renderCtx);
    });

    renderCtx.finish();

    recordOutputTransform(cmd, scanIdx >= 0 ? scanFbs[scanIdx] : framebuffer, outputDesc, width, height, outputColor, output->colorTemp());

    lastImage = scanIdx >= 0 ? output->scanoutBuffer(scanIdx)->image : target;
    lastLayout = scanIdx >= 0 ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    if (output->presentNeedsPixels()) {
        // the copy must see the finished render pass output (same barrier as
        // rasterizeShape), otherwise the readback can catch half-drawn pixels
        VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

        bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bar.oldLayout = lastLayout;
        bar.newLayout = lastLayout;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = lastImage;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

        VkBufferImageCopy region{};

        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(u32)width, (u32)height, 1};
        vkCmdCopyImageToBuffer(cmd, lastImage, lastLayout, readback, 1, &region);
    }

    vkEndCommandBuffer(cmd);

    bool needPresentFence = scanIdx >= 0 && output->supportsRenderFence();
    bool signalOut = hasSyncFd && (needPresentFence || !frameSyncFds.empty());

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.waitSemaphoreCount = (u32)waits.length();
    si.pWaitSemaphores = waits.data();
    si.pWaitDstStageMask = waitStages.data();
    si.signalSemaphoreCount = signalOut ? 1 : 0;
    si.pSignalSemaphores = &syncOut;
    VkResult submitResult = vkQueueSubmit(queue, 1, &si, fence);

    if (submitResult != VK_SUCCESS) {
        *(comp->log) << "imway: Vulkan queue submit failed ("_sv << (long)submitResult << ")"_sv << endL;
        ev_break(loop, EVBREAK_ALL);

        return false;
    }

    frameInFlight = true;
    haveFrame = true;

    for (Surface* s : explicitSurfaces) {
        s->syncAcquireWait = false;
    }

    for (SurfaceTexture* tex : externalFirstUses) {
        tex->firstUse = false;
    }

    forEach<Surface, SceneNode>(scene->surfaces, [&](Surface& s) {
        FrameResource* frame = s.frame;

        if (surfaceVisible(&s) && frame && !contains(inFlightFrames, frame)) {
            frameRef(frame);
            inFlightFrames.pushBack(frame);
        }
    });

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

            if (needPresentFence) {
                presentFenceFd = outFd;
            } else {
                close(outFd);
            }
        } else {
            // sync-fd export is what resets a binary semaphore; when it fails
            // the payload stays signalled and the next frame would signal it
            // again without a wait in between. The signal op is still in
            // flight, so recreate after the frame fence retires.
            syncOutStale = true;
        }
    }

    // The dumb-buffer backend consumes the readback on the CPU immediately.
    // Zero-copy/headless paths keep the submission asynchronous and retire it
    // at the next fence poll or presentation completion.
    if (output->presentNeedsPixels()) {
        finishGpuFrame(true);
    }

    return true;
}

// pull the last presented image into the readback buffer; false = the copy
// submit failed, readbackMap holds stale bytes and the fence never signals
// (so we must not enter vkWaitForFences)
bool RendererImpl::readbackLastFrame() {
    if (output->presentNeedsPixels()) {
        return true; // the dumb-buffer path already reads back every frame
    }

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

    if (VkResult res = vkQueueSubmit(queue, 1, &si, fence); res != VK_SUCCESS) {
        *(comp->log) << "imway: readback submit failed ("_sv << (long)res << ")"_sv << endL;

        return false;
    }

    vkWaitOrDie(device, fence, "readback");
    vkResetFences(device, 1, &fence);

    return true;
}

bool RendererImpl::screenshot(StringView path) {
    if (lastFrameDirect) {
        forceComposition = true;
        scene->needsFrame = true;
        frameNow();
        forceComposition = false;
    }

    if (!haveFrame) {
        return false;
    }

    finishGpuFrame(true);

    if (!readbackLastFrame()) {
        return false;
    }

    ScopedFD f(open(Buffer(path).cStr(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));

    if (f.get() < 0) {
        return false;
    }

    FDRegular out(f);
    auto& hdr = sb();

    hdr << "P6\n"_sv << width << " "_sv << height << "\n255\n"_sv;
    out.write(hdr.data(), hdr.used());

    auto* px = (const unsigned char*)readbackMap;
    Vector<u8> row;

    row.zero((size_t)width * 3);

    for (int y = 0; y < height; y++) {
        const unsigned char* src = px + (size_t)y * width * 4;

        if (fmt == VK_FORMAT_A2R10G10B10_UNORM_PACK32) {
            const u32* p = (const u32*)src;

            for (int x = 0; x < width; x++) {
                row.mut(x * 3 + 0) = (u8)((p[x] >> 22) & 0xff);
                row.mut(x * 3 + 1) = (u8)((p[x] >> 12) & 0xff);
                row.mut(x * 3 + 2) = (u8)((p[x] >> 2) & 0xff);
            }
        } else {
            for (int x = 0; x < width; x++) {
                row.mut(x * 3 + 0) = src[x * 4 + 2];
                row.mut(x * 3 + 1) = src[x * 4 + 1];
                row.mut(x * 3 + 2) = src[x * 4 + 0];
            }
        }

        out.write(row.data(), row.length());
    }

    out.finish();

    return true;
}

u64 RendererImpl::colorIntermediateBytes() {
    return 0;
}

// the copy-capture path: same readback as screenshot, but into caller
// memory as XRGB8888 rows. A direct-scanout frame has no composed image to
// read: arm composition for the next frame and report "not this one"
bool RendererImpl::captureSubmit(int rx, int ry, int rw, int rh, Listener& done) {
    if (lastFrameDirect) {
        forceComposition = true;
        scene->needsFrame = true;

        return false;
    }

    if (!haveFrame || rx < 0 || ry < 0 || rw <= 0 || rh <= 0 || rx + rw > width || ry + rh > height) {
        return false;
    }

    if (captureFencePoll->armed() && captureSeq != scene->framesDone) {
        // an older frame's copy is still on the GPU: retry on the next one
        scene->needsFrame = true;

        return false;
    }

    if (!captureFencePoll->armed() && !captureRecord()) {
        return false;
    }

    captureWants.pushBack({&done, rx, ry, rw, rh});

    return true;
}

// one full-frame GPU copy of the last presented image, retired by the
// capture timer; every consumer registered this frame reads from it
bool RendererImpl::captureRecord() {
    if (captureBuf && (captureW != width || captureH != height)) {
        vkDestroyBuffer(device, captureBuf, nullptr);
        vkFreeMemory(device, captureMem, nullptr);
        captureBuf = VK_NULL_HANDLE;
        captureMem = VK_NULL_HANDLE;
        captureMap = nullptr;
    }

    if (!captureBuf) {
        createHostBuffer((VkDeviceSize)width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, captureBuf, captureMem, &captureMap);
        captureW = width;
        captureH = height;
    }

    vkResetCommandBuffer(captureCmd, 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(captureCmd, &bi);

    // the copy trails the frame on the same queue; the barrier makes the
    // rendered pixels visible to the transfer without a host wait
    VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.oldLayout = lastLayout;
    bar.newLayout = lastLayout;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = lastImage;
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(captureCmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);

    VkBufferImageCopy region{};

    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {(u32)width, (u32)height, 1};
    vkCmdCopyImageToBuffer(captureCmd, lastImage, lastLayout, captureBuf, 1, &region);
    vkEndCommandBuffer(captureCmd);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

    si.commandBufferCount = 1;
    si.pCommandBuffers = &captureCmd;

    vkResetFences(device, 1, &captureFence);

    if (VkResult res = vkQueueSubmit(queue, 1, &si, captureFence); res != VK_SUCCESS) {
        *(comp->log) << "imway: capture submit failed ("_sv << (long)res << ")"_sv << endL;

        return false;
    }

    captureSeq = scene->framesDone;
    captureFencePoll->arm();

    return true;
}

void RendererImpl::captureRetired(VkResult status) {
    if (status != VK_SUCCESS) {
        *(comp->log) << "imway: capture fence failed ("_sv << (long)status << ")"_sv << endL;
    } else if (fmt == VK_FORMAT_A2R10G10B10_UNORM_PACK32) {
        // consumers get XRGB8888: collapse the 10-bit scanout in place
        u32* px = (u32*)captureMap;

        for (size_t i = 0; i < (size_t)captureW * captureH; i++) {
            u32 p = px[i];

            px[i] = (((p >> 22) & 0xffu) << 16) | (((p >> 12) & 0xffu) << 8) | ((p >> 2) & 0xffu) | 0xff000000u;
        }
    }

    // composition was only forced for the capture; give scanout back
    // unless the interactive screenshot still needs it
    if (!shotRequested) {
        forceComposition = false;
    }

    for (size_t i = 0; i < captureWants.length(); i++) {
        const CaptureWant& want = captureWants[i];

        if (status == VK_SUCCESS) {
            CaptureRows rows;

            rows.base = (const unsigned char*)captureMap + ((size_t)want.y * captureW + want.x) * 4;
            rows.stride = (size_t)captureW * 4;
            want.done->onListen(&rows);
        } else {
            want.done->onListen(nullptr);
        }
    }

    captureWants.clear();
}

void RendererImpl::captureCancel(Listener& done) {
    for (size_t i = 0; i < captureWants.length(); i++) {
        if (captureWants[i].done == &done) {
            captureWants.mut(i) = captureWants.back();
            captureWants.popBack();

            return;
        }
    }
}

// the mode changed under the consumers: fail them, drop the sized buffer
void RendererImpl::captureDropAll() {
    // the backend idled the GPU before the rebuild: the fence is done and
    // the next record resets it
    captureFencePoll->cancel();

    for (size_t i = 0; i < captureWants.length(); i++) {
        captureWants[i].done->onListen(nullptr);
    }

    captureWants.clear();
    vkDestroyBuffer(device, captureBuf, nullptr);
    vkFreeMemory(device, captureMem, nullptr);
    captureBuf = VK_NULL_HANDLE;
    captureMem = VK_NULL_HANDLE;
    captureMap = nullptr;
    captureW = 0;
    captureH = 0;
}

// one-pixel eyedropper: reuse the screenshot readback of the last frame,
// then decode the pixel at (x,y) per the scanout format
bool RendererImpl::readPixel(int x, int y, u8& r, u8& g, u8& b) {
    if (!haveFrame || x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }

    finishGpuFrame(true);

    if (!readbackLastFrame()) {
        return false;
    }

    const unsigned char* src = (const unsigned char*)readbackMap + ((size_t)y * width + x) * 4;

    if (fmt == VK_FORMAT_A2R10G10B10_UNORM_PACK32) {
        u32 p = *(const u32*)src;

        r = (u8)((p >> 22) & 0xff);
        g = (u8)((p >> 12) & 0xff);
        b = (u8)((p >> 2) & 0xff);
    } else {
        r = src[2];
        g = src[1];
        b = src[0];
    }

    return true;
}

void RendererImpl::captureScreenshot() {
    if (shotRequested || shotCapture->busy()) {
        return;
    }

    shotCapture->request();
}

void RendererImpl::beginScreenshot() {
    shotRequested = true;
    forceComposition = true;
    scene->needsFrame = true;
}

void RendererImpl::frameNow() {
    if (width <= 0) {
        // no mode announced yet: nothing to render into
        return;
    }

    if (!finishGpuFrame(false)) {
        scene->needsFrame = true;

        return;
    }

    // deferred cursor rasterization: the previous frame's fence has
    // retired, so the queue is idle and the imgui backend's shared vertex
    // buffers are free — the queue wait inside rasterizeShape is a no-op
    // here, where mid-frame it would drain the whole pipeline
    if (pendingShape >= 0) {
        int kind = pendingShape;

        pendingShape = -1;

        if (hwCursorReady && output->cursorCapW() > 0) {
            Vector<u32>& img = hwShapeCache[kind];

            if (!img.length()) {
                img.zero((size_t)hwCapW * hwCapH);
                rasterizeShape(kind, img.mutData());
            }

            output->setCursorImage(img.data());
            hwKind = kind;
            hwSurf.reset();
        }
    }

    timespec ft0{};

    clock_gettime(CLOCK_MONOTONIC, &ft0);

    if (scene->needsFrame) {
        settleFrames = 3;
    }

    scene->needsFrame = false;
    settleFrames--;

    forEach<Surface, SceneNode>(scene->surfaces, [&](Surface& s) {
        if (s.dirty && s.hasContent) {
            bool ready = true;

            if (s.dmabuf) {
                ready = importDmabuf(s);
            } else {
                uploadSurface(s);
            }

            if (&s == scene->cursorSurface) {
                hwSurfStale = true;
            }

            s.dirty = !ready;

            if (!ready) {
                scene->needsFrame = true;
            }
        }
    });

    HdrContentMetadata contentMetadata;
    const OutputColorState& outputColor = output->colorState();

    // The desktop and compositor UI are SDR content even when no client is
    // visible. Client metadata is advisory, but it lets the output describe
    // the result of the same display mapping that the pixels pass through.
    contentMetadata.add(ColorDescription::sRgb(), outputColor.sdrWhiteNits);

    double white = outputColor.hdr() ? outputColor.sdrWhiteNits : 203.0;

    sceneMaxNits = white;
    forEach<Surface, SceneNode>(scene->surfaces, [&](Surface& s) {
        if (s.hasContent && surfaceVisible(&s)) {
            contentMetadata.add(s.color, outputColor.sdrWhiteNits);

            double nits = surfaceMaxNits(s.color, white);

            sceneMaxNits = nits > sceneMaxNits ? nits : sceneMaxNits;
        }
    });
    output->setHdrMetadata(hdrOutputMetadata(outputColor, contentMetadata));

    Surface* cand = scanoutCandidate();

    scene->scanoutCandidateId = cand && cand->toplevel ? cand->toplevel->id : 0;

    // let a fullscreen client that opted into tearing get async page flips
    output->setTearingHint(cand && cand->tearingAsync);

    bool direct = cand && output->directScanout(cand->dmabuf, cand->frame);

    lastFrameDirect = direct;

    if (!direct) {
        int idx = output->scanoutCount() > 0 ? output->acquire() : -1;
        bool accepted = idx < 0;

        if (!renderFrame(idx)) {
            scene->needsFrame = true;

            return;
        }

        if (idx >= 0) {
            // Older KMS drivers do not expose IN_FENCE_FD, and exporting a
            // sync_file can also fail.  Preserve correctness in both cases
            // by waiting on the CPU before handing the framebuffer to KMS.
            if (presentFenceFd < 0 && !finishGpuFrame(true)) {
                scene->needsFrame = true;

                return;
            }

            accepted = output->presentImage(idx, presentFenceFd);

            if (presentFenceFd >= 0) {
                close(presentFenceFd);
                presentFenceFd = -1;
            }
        } else {
            output->present(output->presentNeedsPixels() ? readbackMap : nullptr);
        }

        if (shotRequested && accepted) {
            if (shotCapture->submit(idx, lastImage, lastLayout)) {
                shotRequested = false;
                forceComposition = false;
            } else {
                scene->needsFrame = true;
            }
        }
    }

    timespec ft1{};

    clock_gettime(CLOCK_MONOTONIC, &ft1);
    frameMs[frameMsIdx] = (float)((double)(ft1.tv_sec - ft0.tv_sec) * 1e3 + (double)(ft1.tv_nsec - ft0.tv_nsec) / 1e6);
    frameMsIdx = (frameMsIdx + 1) % kFrameHistory;

    scene->framesDone++;

    if (framesLimit > 0 && scene->framesDone >= framesLimit) {
        ev_break(loop, EVBREAK_ALL);
    }
}

void RendererImpl::tick() {
    // headless present() reports the frame event before the GPU is done, so the
    // frame retires here instead; without this, dmabuf releases to clients
    // wait for the next needed frame or the 2s clock tick
    finishGpuFrame(false);

    if (wantFrame()) {
        frameNow();

        return;
    }

    scene->framesDone++;

    if (framesLimit > 0 && scene->framesDone >= framesLimit) {
        ev_break(loop, EVBREAK_ALL);
    }
}

Renderer* Renderer::create(Composer& c, const DeviceVk& vk, StringView fontPath, float uiScale, int framesLimit) {
    return c.pool->make<RendererImpl>(c, vk, fontPath, uiScale, framesLimit);
}
