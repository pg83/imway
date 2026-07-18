#include "composer.h"
#include "renderer.h"
#include "wayland.h"

#include "calendar.h"
#include "device_vk.h"
#include "frame_listener.h"
#include "input_sink.h"
#include "keyboard.h"
#include "icon.h"
#include "icon_pool.h"
#include "icon_store.h"
#include "inspector.h"
#include "launcher.h"
#include "mixer.h"
#include "history.h"
#include "notifier.h"
#include "osd.h"
#include "pooled_ev.h"
#include "pooled_vk.h"
#include "settings.h"
#include "wifi.h"
#include "wifi_ui.h"
#include "output.h"
#include "intr_list.h"
#include "scene.h"
#include "shadow.h"
#include "toast.h"
#include "util.h"

#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>

#include <linux/dma-buf.h>

#include <ev.h>
#include <linux/input-event-codes.h>

#include <vulkan/vulkan.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xdg-shell-server-protocol.h>

#include <cm_convert.spv.h> // generated SPIR-V: cm_convert_spv[]

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>

#include <std/dbg/verify.h>
#include <std/ios/fs_utils.h>
#include <std/ios/out_fd.h>
#include <std/ios/sys.h>
#include <std/sys/fs.h>
#include <std/lib/vector.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/fd.h>

using namespace stl;

struct TextureLease;

// the node links it into the renderer's texture registry
struct SurfaceTexture: stl::IntrusiveNode {
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
    u32 mips = 1;
    bool needsUpload = false;
    bool firstUse = true;
    bool external = false;
    bool arenaOwned = false;
    TextureLease* lease = nullptr;

    // color-management: a surface with a non-sRGB image description is
    // converted into the sRGB composition space by a compute pass. The result
    // lives in convImage; tex->ds then points at convView instead of view.
    VkImage convImage = VK_NULL_HANDLE;
    VkDeviceMemory convMemory = VK_NULL_HANDLE;
    VkImageView convView = VK_NULL_HANDLE;
    VkDescriptorSet convDs = VK_NULL_HANDLE; // compute src+dst binding
    u32 convGen = 0xffffffffu;               // colorGeneration reflected by convImage
    bool converted = false;                  // ds currently points at convView
    bool convFresh = false;                  // convImage layout still UNDEFINED
};

struct TextureLease {
    void* owner = nullptr;
    SurfaceTexture* texture = nullptr;
    void (*destroy)(void*, SurfaceTexture*) = nullptr;

    TextureLease(void* o, SurfaceTexture* t, void (*d)(void*, SurfaceTexture*));
    ~TextureLease() noexcept;
};


namespace {
    void frameTimerCb(struct ev_loop*, ev_timer* w, int);
    void prepareCb(struct ev_loop*, ev_prepare* w, int);
    void clockTimerCb(struct ev_loop*, ev_timer* w, int);


    struct RendererImpl: public Renderer, public InputSink, public IconResolver, public MixerListener, public WifiListener, public FrameListener {
        InputSink* sink() override;


        void modsChanged() override;

        struct ev_loop* loop = nullptr;
        stl::ObjPool* pool = nullptr;
        Scene* scene = nullptr;
        ::Output* output = nullptr;
        FrameListener* listener = nullptr;
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
        bool frameInFlight = false;
        Vector<FrameResource*> inFlightFrames;
        VkSampler sampler = VK_NULL_HANDLE;

        // color-management conversion compute pipeline
        VkDescriptorSetLayout cmSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout cmPipeLayout = VK_NULL_HANDLE;
        VkPipeline cmPipeline = VK_NULL_HANDLE;
        VkDescriptorPool cmDescPool = VK_NULL_HANDLE;

        ObjList<SurfaceTexture> textureAlloc;
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

        StringView fontPath;
        float uiScale = 1.f;
        float nextUiScale = 1.f;   // written by the ui, applied at frame start
        Settings settings;         // menu state incl. change flags, see settings.h
        ShadowSprite shadow;       // window drop shadows, see shadow.h

        // bar widgets: /proc-fed cpu, meminfo, battery; sampled at most
        // once per ~2s, the clock timer keeps frames coming
        u64 statMs = 0;
        u64 cpuPrevBusy = 0, cpuPrevTotal = 0;
        int cpuPct = 0;
        long memUsedMb = 0;
        long batPct = -1;          // -1 no battery
        bool batCharging = false;
        StringBuilder batPath;

        // calendar: opaque dialog handle owned here, state lives in the
        // widget; non-null = open
        void* calendarState = nullptr;
        bool calendarToggle = false;

        Notifier* notifier = nullptr;
        Composer* comp = nullptr;

        // osd: armed by mixer/backlight changes, fades at the tail
        u64 osdMs = 0;
        int osdKind = 0; // 1 volume, 2 brightness

        // wifi picker: opaque dialog handle owned here
        void* wifiState = nullptr;
        bool wifiToggle = false;

        // inspector (Super+F12): opaque dialog handle owned here, state
        // lives in the widget; non-null = open
        void* inspectorState = nullptr;
        bool inspectorToggle = false;
        void* historyState = nullptr;
        bool historyToggle = false;

        // color picker (eyedropper): armed from the view menu, the next
        // click samples the framebuffer pixel under the cursor
        bool pickArmed = false;
        bool pickPending = false;
        int pickX = 0, pickY = 0;
        bool pickShow = false;
        bool forceComposition = false;
        bool lastFrameDirect = false;
        u8 pickR = 0, pickG = 0, pickB = 0;
        float frameMs[kFrameHistory] = {};
        int frameMsIdx = 0;

        // input mastering: imgui first, leftovers to the wayland slave sink
        Keyboard* keyboard = nullptr;
        InputSink* next = nullptr;

        // the cursor position lives here: sources emit raw deltas, this is
        // the code that integrates them and applies lock/confine policy
        double posX = 0, posY = 0;
        bool kbCapturePrev = false;
        bool chordDown[256] = {};

        // alt-tab overlay: selection commits on Alt release
        bool altTabActive = false;
        Toplevel* altTabSel = nullptr;

        // launcher: opaque dialog handle owned here, state lives in the
        // widget; non-null = open
        void* launcherState = nullptr;
        bool launcherToggle = false;
        IconStore* icons = nullptr;

        // hardware cursor plane state
        bool hwCursorReady = false;
        int hwCapW = 0, hwCapH = 0;
        int hwHotX = 0, hwHotY = 0;
        bool hwVisible = false;
        int hwKind = -2;               // ImGuiMouseCursor of the uploaded image; -2 nothing, -3 client surface
        int pendingShape = -1;         // shape waiting for end-of-frame rasterization
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
        void setup(int w, int h);

        void setupColorConvert();
        void ensureConversion(SurfaceTexture* tex, Surface& s);
        void recordConversion(VkCommandBuffer cb, SurfaceTexture* tex, Surface& s);
        void freeConversion(SurfaceTexture* tex);

        void drawSurfaceTree(Surface& s, float x, float y);
        void drawSurfaceTreeOverlay(Surface& s, float x, float y);
        void markTreeUnhovered(Surface& s);
        void buildUi(Scene& scene);
        void sampleStats();
        void volumeChanged() override;
        void wifiChanged() override;
        void cursorUi(Scene& scene, bool overClient);
        void rasterizeShape(int kind, u32* out);

        void clampPos();

        // every event feeds imgui first; whatever the compositor ui did not
        // capture flows on to the wayland slave sink
        void motion(double x, double y) override;
        void button(u32 btn, bool pressed) override;
        void key(u32 code, bool pressed) override;
        void scroll(const ScrollEvent& ev) override;

        // the relative stream always reaches the slave (locked-pointer
        // clients live off it); the visible cursor only moves when no lock
        // is active — that policy belongs here, not in the source
        void relMotion(double dx, double dy, double dxRaw, double dyRaw) override;
        void absMotion(double nx, double ny) override;
        void swipeBegin(u32 fingers) override;
        void swipeUpdate(double dx, double dy) override;
        void swipeEnd(bool cancelled) override;
        void pinchBegin(u32 fingers) override;
        void pinchUpdate(double dx, double dy, double scale, double rotation) override;
        void pinchEnd(bool cancelled) override;
        void holdBegin(u32 fingers) override;
        void holdEnd(bool cancelled) override;

        // IconResolver: gen -> texture, textures are born on first use
        u64 iconTexture(const Icon* icon) override;
        SurfaceTexture* makeIconTexture(const u32* argb, int w, int h);

        bool chordAction(u32 mask, u32 sym);
        void altTabStep(long dir);
        void altTabCommit();

        bool importDmabuf(Surface& s);
        void uploadSurface(Surface& s);
        void destroyTexture(SurfaceTexture* tex);
        SurfaceTexture* cacheFind(DmabufBuffer* b);
        bool cacheContainsTex(const SurfaceTexture* tex) const;
        void releaseSurfaceTexture(Surface& s);
        Surface* scanoutCandidate();
        bool surfaceVisible(Surface* s) const;
        bool finishGpuFrame(bool wait);

        bool wantFrame() const;

        void frameNow();
        void frameShown(u32 msec) override;
        bool renderFrame(int scanIdx);
        bool readbackLastFrame();
        bool screenshot(StringView path) override;
        bool readPixel(int x, int y, u8& r, u8& g, u8& b);
        void captureScreenshot();
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

    // the desktop renders on demand, wake it up so the clock stays fresh
    void clockTimerCb(struct ev_loop*, ev_timer* w, int) {
        ((RendererImpl*)w->data)->scene->needsFrame = true;
    }
}

TextureLease::TextureLease(void* o, SurfaceTexture* t, void (*d)(void*, SurfaceTexture*))
    : owner(o)
    , texture(t)
    , destroy(d)
{
}

TextureLease::~TextureLease() noexcept {
    if (texture) {
        destroy(owner, texture);
    }
}

InputSink* RendererImpl::sink() {
    return this;
}

void RendererImpl::modsChanged() {
}

RendererImpl::RendererImpl(Composer& comp, const DeviceVk& vk, StringView font, float scale, int limit)
    : comp(&comp)
    , loop(comp.loop)
    , keyboard(comp.kb)
    , next(comp.wayland->sink())
    , pool(comp.pool)
    , scene(comp.scene)
    , output(comp.output)
    , listener(comp.wayland->frameListener())
    , icons(comp.icons)
    , notifier(comp.notifier)
    , framesLimit(limit)
    , instance(vk.instance)
    , phys(vk.phys)
    , device(vk.device)
    , queueFamily(vk.queueFamily)
    , queue(vk.queue)
    , textureAlloc(comp.pool)
    , hasDmabuf(vk.hasDmabuf)
    , getMemoryFdProps(vk.getMemoryFdProps)
{
    fontPath = font;
    uiScale = scale;
    nextUiScale = scale;
    hasSyncFd = vk.hasSyncFd;
    drmFd = vk.drmFd;
    comp.iconResolver = this;
    comp.mixerListeners.pushBack((MixerListener*)this);
    comp.wifiListeners.pushBack((WifiListener*)this);
    setup(scene->outW, scene->outH);
    output->setFrameListener(this);

    // before any input arrives the cursor sits at the screen center
    posX = scene->outW / 2.0;
    posY = scene->outH / 2.0;
    ImGui::GetIO().AddMousePosEvent((float)posX, (float)posY);

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
    output->setFrameListener(nullptr);

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
}

void RendererImpl::clampPos() {
    posX = posX < 0 ? 0 : posX >= width ? width - 1 : posX;
    posY = posY < 0 ? 0 : posY >= height ? height - 1 : posY;

    if (!scene->pointerConfined || scene->confineRegion.empty()) {
        return;
    }

    double bestX = posX, bestY = posY;
    double bestDist = -1;

    for (const RectI& r : scene->confineRegion) {
        double x0 = r.x, y0 = r.y;
        double x1 = (double)r.x + r.w - 1;
        double y1 = (double)r.y + r.h - 1;
        double x = posX < x0 ? x0 : posX > x1 ? x1 : posX;
        double y = posY < y0 ? y0 : posY > y1 ? y1 : posY;
        double dx = posX - x, dy = posY - y;
        double dist = dx * dx + dy * dy;

        if (bestDist < 0 || dist < bestDist) {
            bestDist = dist;
            bestX = x;
            bestY = y;
        }
    }

    posX = bestX;
    posY = bestY;
}

void RendererImpl::motion(double x, double y) {
    posX = x;
    posY = y;
    clampPos();
    scene->needsFrame = true;
    ImGui::GetIO().AddMousePosEvent((float)posX, (float)posY);

    // keep the plane position fresh for the next frame commit
    if (hwCursorReady && hwVisible) {
        output->setCursorPos((int)posX - hwHotX, (int)posY - hwHotY, true);
    }

    // the slave always needs the position: it turns ui capture into
    // a proper pointer leave on its own
    if (next) {
        next->motion(posX, posY);
    }
}

void RendererImpl::button(u32 btn, bool pressed) {
    if (pickArmed && pressed && btn == BTN_LEFT) {
        pickArmed = false;
        pickPending = true;
        pickX = (int)posX;
        pickY = (int)posY;
        scene->needsFrame = true;

        return; // the click is the sample, not a click for anyone
    }

    // side/extra buttons mean nothing to ImGui; mapping them onto MIDDLE
    // both faked middle clicks in the ui and swallowed them under capture
    if (btn != BTN_LEFT && btn != BTN_RIGHT && btn != BTN_MIDDLE) {
        if (next) {
            next->button(btn, pressed);
        }

        return;
    }

    int imguiBtn = btn == BTN_LEFT ? 0 : btn == BTN_RIGHT ? 1 : 2;

    scene->needsFrame = true;
    ImGui::GetIO().AddMouseButtonEvent(imguiBtn, pressed);

    // presses over our ui stay ours; releases always go through, the
    // slave drops the ones whose press it never saw
    if (next && (!pressed || !scene->ptrCaptured)) {
        next->button(btn, pressed);
    }
}

void RendererImpl::scroll(const ScrollEvent& ev) {
    scene->needsFrame = true;
    ImGui::GetIO().AddMouseWheelEvent((float)-ev.dx, (float)-ev.dy);

    if (next && !scene->ptrCaptured) {
        next->scroll(ev);
    }
}

void RendererImpl::relMotion(double dx, double dy, double dxRaw, double dyRaw) {
    if (next) {
        next->relMotion(dx, dy, dxRaw, dyRaw);
    }

    if (!scene->pointerLocked) {
        motion(posX + dx, posY + dy);
    }
}

void RendererImpl::absMotion(double nx, double ny) {
    if (!scene->pointerLocked) {
        motion(nx * width, ny * height);
    }
}

void RendererImpl::swipeBegin(u32 fingers) {
    if (next) {
        next->swipeBegin(fingers);
    }
}

void RendererImpl::swipeUpdate(double dx, double dy) {
    if (next) {
        next->swipeUpdate(dx, dy);
    }
}

void RendererImpl::swipeEnd(bool cancelled) {
    if (next) {
        next->swipeEnd(cancelled);
    }
}

void RendererImpl::pinchBegin(u32 fingers) {
    if (next) {
        next->pinchBegin(fingers);
    }
}

void RendererImpl::pinchUpdate(double dx, double dy, double scale, double rotation) {
    if (next) {
        next->pinchUpdate(dx, dy, scale, rotation);
    }
}

void RendererImpl::pinchEnd(bool cancelled) {
    if (next) {
        next->pinchEnd(cancelled);
    }
}

void RendererImpl::holdBegin(u32 fingers) {
    if (next) {
        next->holdBegin(fingers);
    }
}

void RendererImpl::holdEnd(bool cancelled) {
    if (next) {
        next->holdEnd(cancelled);
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
    SurfaceTexture* tex = textureAlloc.make();

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
    tex->ds = ImGui_ImplVulkan_AddTexture(sampler, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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

    if (root->toplevel && root->toplevel->mapped) {
        return true;
    }

    for (Popup* popup : each<Popup>(scene->popups)) {
        if (popup->mapped && popup->surface == root) {
            return true;
        }
    }

    return root == scene->cursorSurface || root == scene->dragIcon;
}

bool RendererImpl::finishGpuFrame(bool wait) {
    if (!frameInFlight) {
        return true;
    }

    VkResult status = wait ? vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) : vkGetFenceStatus(device, fence);

    if (status == VK_NOT_READY) {
        return false;
    }

    if (status != VK_SUCCESS) {
        sysE << "imway: Vulkan frame fence failed ("_sv << (long)status << ")"_sv << endL;
        ev_break(loop, EVBREAK_ALL);

        return false;
    }

    status = vkResetFences(device, 1, &fence);

    if (status != VK_SUCCESS) {
        sysE << "imway: Vulkan frame fence reset failed ("_sv << (long)status << ")"_sv << endL;
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

void RendererImpl::frameShown(u32 msec) {
    finishGpuFrame(false);

    if (listener) {
        listener->frameShown(msec);
    }
}

namespace {
    ImGuiKey evdevToImGuiKey(u32 code) {
        switch (code) {
            case KEY_A: return ImGuiKey_A; case KEY_B: return ImGuiKey_B; case KEY_C: return ImGuiKey_C;
            case KEY_D: return ImGuiKey_D; case KEY_E: return ImGuiKey_E; case KEY_F: return ImGuiKey_F;
            case KEY_G: return ImGuiKey_G; case KEY_H: return ImGuiKey_H; case KEY_I: return ImGuiKey_I;
            case KEY_J: return ImGuiKey_J; case KEY_K: return ImGuiKey_K; case KEY_L: return ImGuiKey_L;
            case KEY_M: return ImGuiKey_M; case KEY_N: return ImGuiKey_N; case KEY_O: return ImGuiKey_O;
            case KEY_P: return ImGuiKey_P; case KEY_Q: return ImGuiKey_Q; case KEY_R: return ImGuiKey_R;
            case KEY_S: return ImGuiKey_S; case KEY_T: return ImGuiKey_T; case KEY_U: return ImGuiKey_U;
            case KEY_V: return ImGuiKey_V; case KEY_W: return ImGuiKey_W; case KEY_X: return ImGuiKey_X;
            case KEY_Y: return ImGuiKey_Y; case KEY_Z: return ImGuiKey_Z;
            case KEY_1: return ImGuiKey_1; case KEY_2: return ImGuiKey_2; case KEY_3: return ImGuiKey_3;
            case KEY_4: return ImGuiKey_4; case KEY_5: return ImGuiKey_5; case KEY_6: return ImGuiKey_6;
            case KEY_7: return ImGuiKey_7; case KEY_8: return ImGuiKey_8; case KEY_9: return ImGuiKey_9;
            case KEY_0: return ImGuiKey_0;
            case KEY_F1: return ImGuiKey_F1; case KEY_F2: return ImGuiKey_F2; case KEY_F3: return ImGuiKey_F3;
            case KEY_F4: return ImGuiKey_F4; case KEY_F5: return ImGuiKey_F5; case KEY_F6: return ImGuiKey_F6;
            case KEY_F7: return ImGuiKey_F7; case KEY_F8: return ImGuiKey_F8; case KEY_F9: return ImGuiKey_F9;
            case KEY_F10: return ImGuiKey_F10; case KEY_F11: return ImGuiKey_F11; case KEY_F12: return ImGuiKey_F12;
            case KEY_LEFT: return ImGuiKey_LeftArrow; case KEY_RIGHT: return ImGuiKey_RightArrow;
            case KEY_UP: return ImGuiKey_UpArrow; case KEY_DOWN: return ImGuiKey_DownArrow;
            case KEY_HOME: return ImGuiKey_Home; case KEY_END: return ImGuiKey_End;
            case KEY_PAGEUP: return ImGuiKey_PageUp; case KEY_PAGEDOWN: return ImGuiKey_PageDown;
            case KEY_INSERT: return ImGuiKey_Insert; case KEY_DELETE: return ImGuiKey_Delete;
            case KEY_BACKSPACE: return ImGuiKey_Backspace; case KEY_TAB: return ImGuiKey_Tab;
            case KEY_ENTER: return ImGuiKey_Enter; case KEY_KPENTER: return ImGuiKey_KeypadEnter;
            case KEY_ESC: return ImGuiKey_Escape; case KEY_SPACE: return ImGuiKey_Space;
            case KEY_MINUS: return ImGuiKey_Minus; case KEY_EQUAL: return ImGuiKey_Equal;
            case KEY_LEFTBRACE: return ImGuiKey_LeftBracket; case KEY_RIGHTBRACE: return ImGuiKey_RightBracket;
            case KEY_SEMICOLON: return ImGuiKey_Semicolon; case KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
            case KEY_GRAVE: return ImGuiKey_GraveAccent; case KEY_BACKSLASH: return ImGuiKey_Backslash;
            case KEY_COMMA: return ImGuiKey_Comma; case KEY_DOT: return ImGuiKey_Period;
            case KEY_SLASH: return ImGuiKey_Slash; case KEY_CAPSLOCK: return ImGuiKey_CapsLock;
            case KEY_LEFTSHIFT: return ImGuiKey_LeftShift; case KEY_RIGHTSHIFT: return ImGuiKey_RightShift;
            case KEY_LEFTCTRL: return ImGuiKey_LeftCtrl; case KEY_RIGHTCTRL: return ImGuiKey_RightCtrl;
            case KEY_LEFTALT: return ImGuiKey_LeftAlt; case KEY_RIGHTALT: return ImGuiKey_RightAlt;
            case KEY_LEFTMETA: return ImGuiKey_LeftSuper; case KEY_RIGHTMETA: return ImGuiKey_RightSuper;
            default: return ImGuiKey_None;
        }
    }
}

void RendererImpl::key(u32 code, bool pressed) {
    scene->needsFrame = true;

    if (keyboard) {
        keyboard->updateKey(code, pressed);
    }

    ImGuiIO& io = ImGui::GetIO();
    u32 mask = keyboard ? keyboard->modMask() : 0;

    // modifiers reach imgui even for consumed keys
    io.AddKeyEvent(ImGuiMod_Ctrl, mask & kModCtrl);
    io.AddKeyEvent(ImGuiMod_Shift, mask & kModShift);
    io.AddKeyEvent(ImGuiMod_Alt, mask & kModAlt);
    io.AddKeyEvent(ImGuiMod_Super, mask & kModLogo);

    // media keys are compositor-global regardless of focus or capture;
    // releases fall through — the slave drops unmatched ones
    if (pressed && output->hasBrightness() && (code == KEY_BRIGHTNESSUP || code == KEY_BRIGHTNESSDOWN)) {
        float d = code == KEY_BRIGHTNESSUP ? 0.05f : -0.05f;

        output->setBrightness(output->brightness() + d);
        osdMs = nowMsec() + 1500;
        osdKind = 2;
        scene->needsFrame = true;

        return;
    }

    if (pressed && comp->mixer) {
        Mixer* mx = comp->mixer;

        if (code == KEY_VOLUMEUP || code == KEY_VOLUMEDOWN) {
            float d = code == KEY_VOLUMEUP ? 0.05f : -0.05f;
            float v = mx->volume() + d;

            mx->setVolume(v < 0.f ? 0.f : v > 1.f ? 1.f : v);
            // the osd shows even when the value pinned at a limit: mashing
            // volume-up at max still deserves feedback
            volumeChanged();

            return;
        }

        if (code == KEY_MUTE) {
            mx->setMuted(!mx->muted());
            volumeChanged();

            return;
        }
    }

    if (altTabActive && !pressed && (code == KEY_LEFTALT || code == KEY_RIGHTALT)) {
        altTabActive = false;
        altTabSel = nullptr;
        scene->needsFrame = true;
    }

    // 1. compositor-global chords are sacred: consumed before anyone,
    // imgui included, matched on the group-0 keysym so they work in
    // any layout
    if (!scene->shortcutsInhibited && next && keyboard && code < 256) {
        if (altTabActive && pressed && keyboard->keysymBase(code) == XKB_KEY_Escape) {
            altTabActive = false;
            altTabSel = nullptr;
            scene->needsFrame = true;
            chordDown[code] = true;
            next->modsChanged();

            return;
        }

        if (pressed && chordAction(mask, keyboard->keysymBase(code))) {
            chordDown[code] = true;
            next->modsChanged();

            return;
        }

        if (!pressed && chordDown[code]) {
            chordDown[code] = false;

            // switching happens on Tab release; Alt only holds the overlay
            if (altTabActive && keyboard->keysymBase(code) == XKB_KEY_Tab) {
                altTabCommit();
            }

            next->modsChanged();

            return;
        }
    }

    // 2. imgui is fed the rest unconditionally: physical key + text
    if (ImGuiKey k = evdevToImGuiKey(code); k != ImGuiKey_None) {
        io.AddKeyEvent(k, pressed);
    }

    if (pressed && keyboard) {
        char buf[8];

        if (keyboard->utf8(code, buf, sizeof(buf)) > 0 && (u8)buf[0] >= 0x20) {
            io.AddInputCharactersUTF8(buf);
        }
    }

    if (!next) {
        return;
    }

    // 3. ui capture gate (last-frame imgui truth, kwin-style edge handling
    // lives in the slave's modsChanged). NOT WantCaptureKeyboard: imgui keys
    // that off ActiveId, and any mouse-hold over a window parks ActiveId on
    // the window's MoveId — even a press inside client content — which would
    // swallow every key typed during a drag. What matters is typing intent:
    // an active text field, or one of our own overlays.
    bool capture = launcherState || altTabActive || io.WantTextInput;

    scene->kbCaptured = capture;

    // presses under capture stay ours; releases always go through — the
    // slave drops the ones whose press the client never saw, and a release
    // lost to a capture window would leave the client with a stuck key
    if (!capture || !pressed) {
        next->key(code, pressed);
    }

    next->modsChanged();
}

void RendererImpl::wifiChanged() {
    scene->needsFrame = true;
}

void RendererImpl::volumeChanged() {
    if (!settings.open) {
        osdMs = nowMsec() + 1500;
        osdKind = 1;
    }

    scene->needsFrame = true;
}

bool RendererImpl::chordAction(u32 mask, u32 sym) {
    if (sym == XKB_KEY_Print) {
        captureScreenshot();

        return true;
    }

    if (mask == kModLogo && sym == XKB_KEY_F2) {
        launcherToggle = true;
        scene->needsFrame = true;

        return true;
    }

    if (mask == kModLogo && sym == XKB_KEY_F12) {
        inspectorToggle = true;
        scene->needsFrame = true;

        return true;
    }

    if (mask == kModAlt && sym == XKB_KEY_Tab) {
        altTabStep(1);

        return true;
    }

    if (mask == (kModAlt | kModShift) && sym == XKB_KEY_Tab) {
        altTabStep(-1);

        return true;
    }

    return false;
}

void RendererImpl::altTabStep(long dir) {
    IntrusiveList& tls = scene->toplevels;

    if (tls.empty()) {
        return;
    }

    Toplevel* base = altTabActive && intrListContains<Toplevel>(tls, altTabSel) ? altTabSel : scene->focusedToplevel;
    // a circular walk over the ring; the base (or the head sentinel when
    // there is no base) both starts and bounds it, and gets re-tested last
    // so a lone mapped window still selects itself
    IntrusiveNode* start = base && !base->singular() ? (IntrusiveNode*)base : tls.mutEnd();
    IntrusiveNode* n = start;

    do {
        n = dir > 0 ? n->next : n->prev;

        if (n == tls.mutEnd()) {
            n = dir > 0 ? n->next : n->prev;
        }

        Toplevel* t = (Toplevel*)n;

        if (n != tls.mutEnd() && t->mapped) {
            altTabActive = true;
            altTabSel = t;
            scene->needsFrame = true;

            return;
        }
    } while (n != start);
}

void RendererImpl::altTabCommit() {
    if (intrListContains<Toplevel>(scene->toplevels, altTabSel) && altTabSel->mapped) {
        altTabSel->raiseRequested = true;
    }

    scene->needsFrame = true;
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

void RendererImpl::setup(int w, int h) {
    width = w;
    height = h;
    scanout = output->scanoutCount() > 0;

    if (scanout) {
        fmt = output->scanoutBuffer(0)->format;
    }

    if (!scanout) {
        createImage(width, height, fmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, target, targetMemory);

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

        vci.image = target;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &vci, nullptr, &targetView));
    }

    createHostBuffer((VkDeviceSize)width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback, readbackMemory, &readbackMap);

    VkAttachmentDescription att{};

    att.format = fmt;
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
            vci.format = fmt;
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

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
    ii.DescriptorPoolSize = 512;
    ii.MinImageCount = 2;
    ii.ImageCount = 2;
    ii.PipelineInfoMain.RenderPass = renderPass;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    STD_VERIFY(ImGui_ImplVulkan_Init(&ii));

    hwCapW = output->cursorCapW();
    hwCapH = output->cursorCapH();

    if (hwCapW > 0 && hwCapH > 0) {
        createImage(hwCapW, hwCapH, fmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, curImg, curImgMem);

        VkImageViewCreateInfo cvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

        cvi.image = curImg;
        cvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        cvi.format = fmt;
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

    setupColorConvert();

    // pool-owned for the renderer's whole life (setup runs once): the pool
    // unwinds these after ~RendererImpl has waited the device idle and shut
    // imgui down, and before DeviceVk dies. LIFO — dependents last.
    pooledVk(*pool, device, renderPass);
    pooledVk(*pool, device, targetMemory);
    pooledVk(*pool, device, target);
    pooledVk(*pool, device, targetView);
    pooledVk(*pool, device, framebuffer);

    for (VkImageView v : scanViews) {
        pooledVk(*pool, device, v);
    }

    for (VkFramebuffer fb : scanFbs) {
        pooledVk(*pool, device, fb);
    }

    pooledVk(*pool, device, readbackMemory);
    pooledVk(*pool, device, readback);
    pooledVk(*pool, device, cmdPool);
    pooledVk(*pool, device, fence);
    pooledVk(*pool, device, sampler);

    for (VkSemaphore sem : syncWaitPool) {
        if (sem) {
            pooledVk(*pool, device, sem);
        }
    }

    if (curImg) {
        pooledVk(*pool, device, curImgMem);
        pooledVk(*pool, device, curImg);
        pooledVk(*pool, device, curView);
        pooledVk(*pool, device, curFb);
        pooledVk(*pool, device, curReadbackMem);
        pooledVk(*pool, device, curReadback);
        pooledVk(*pool, device, curFence);
    }

    pooledVk(*pool, device, cmSetLayout);
    pooledVk(*pool, device, cmPipeLayout);
    pooledVk(*pool, device, cmPipeline);
    pooledVk(*pool, device, cmDescPool);
}

// A compute pipeline that converts a color-managed surface (PQ and/or BT.2020)
// into the sRGB composition space. Bindings: 0 = source sampler2D, 1 = dest
// storage image; push constants carry the transfer/gamut flags + reference
// white. See cm_convert.comp.
void RendererImpl::setupColorConvert() {
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

    smci.codeSize = sizeof(cm_convert_spv);
    smci.pCode = cm_convert_spv;

    VkShaderModule module = VK_NULL_HANDLE;

    VK_CHECK(vkCreateShaderModule(device, &smci, nullptr, &module));

    VkDescriptorSetLayoutBinding binds[2] = {};

    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

    dlci.bindingCount = 2;
    dlci.pBindings = binds;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &dlci, nullptr, &cmSetLayout));

    VkPushConstantRange pcr{};

    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = 3 * sizeof(u32); // int pq, int wide, float refWhite

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

    plci.setLayoutCount = 1;
    plci.pSetLayouts = &cmSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &cmPipeLayout));

    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = module;
    cpci.stage.pName = "main";
    cpci.layout = cmPipeLayout;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &cmPipeline));

    vkDestroyShaderModule(device, module, nullptr);

    VkDescriptorPoolSize sizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256},
    };
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};

    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = 256;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &cmDescPool));
}

// (Re)build the converted image + compute descriptor for a color-managed
// surface, or tear it down when the surface stopped being color-managed.
// Repoints tex->ds so ImGui samples the converted result.
void RendererImpl::ensureConversion(SurfaceTexture* tex, Surface& s) {
    if (!s.colorManaged) {
        if (tex->converted) {
            freeConversion(tex);
            ImGui_ImplVulkan_RemoveTexture(tex->ds);
            tex->ds = ImGui_ImplVulkan_AddTexture(sampler, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            tex->converted = false;
        }
        return;
    }

    if (tex->convImage != VK_NULL_HANDLE && tex->convGen == s.colorGeneration) {
        return; // still valid
    }

    freeConversion(tex);

    createImage(tex->w, tex->h, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, tex->convImage, tex->convMemory);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

    vci.image = tex->convImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(device, &vci, nullptr, &tex->convView));

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};

    dsai.descriptorPool = cmDescPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &cmSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(device, &dsai, &tex->convDs));

    VkDescriptorImageInfo srcInfo{sampler, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo dstInfo{VK_NULL_HANDLE, tex->convView, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[2] = {};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = tex->convDs;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &srcInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = tex->convDs;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &dstInfo;
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    if (tex->converted) {
        ImGui_ImplVulkan_RemoveTexture(tex->ds);
    }
    tex->ds = ImGui_ImplVulkan_AddTexture(sampler, tex->convView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    tex->converted = true;
    tex->convGen = s.colorGeneration;
    tex->convFresh = true;
}

// Record the conversion dispatch for a color-managed surface into cb. The
// source image must already be in SHADER_READ_ONLY_OPTIMAL.
void RendererImpl::recordConversion(VkCommandBuffer cb, SurfaceTexture* tex, Surface& s) {
    // make the just-uploaded source pixels visible to the compute sampler; the
    // upload barriers only published them to the fragment stage
    VkImageMemoryBarrier srcRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    srcRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcRead.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    srcRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    srcRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    srcRead.image = tex->image;
    srcRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, tex->mips, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &srcRead);

    // converted image: (undefined | shader-read) -> general for the compute write
    VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

    toGeneral.srcAccessMask = tex->convFresh ? 0 : VK_ACCESS_SHADER_READ_BIT;
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toGeneral.oldLayout = tex->convFresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image = tex->convImage;
    toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGeneral);
    tex->convFresh = false;

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cmPipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cmPipeLayout, 0, 1, &tex->convDs, 0, nullptr);

    struct {
        i32 pq;
        i32 wide;
        float refWhite;
    } pc = {s.colorPq ? 1 : 0, s.colorWide ? 1 : 0, s.colorRefLum > 0 ? (float)s.colorRefLum : 203.f};

    vkCmdPushConstants(cb, cmPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cb, ((u32)tex->w + 7) / 8, ((u32)tex->h + 7) / 8, 1);

    // converted image: general -> shader-read for ImGui to sample
    VkImageMemoryBarrier toRead = toGeneral;

    toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
}

void RendererImpl::freeConversion(SurfaceTexture* tex) {
    if (tex->convDs != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device, cmDescPool, 1, &tex->convDs);
        tex->convDs = VK_NULL_HANDLE;
    }
    if (tex->convView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, tex->convView, nullptr);
        tex->convView = VK_NULL_HANDLE;
    }
    if (tex->convImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, tex->convImage, nullptr);
        tex->convImage = VK_NULL_HANDLE;
    }
    if (tex->convMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, tex->convMemory, nullptr);
        tex->convMemory = VK_NULL_HANDLE;
    }
    tex->convGen = 0xffffffffu;
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
        releaseSurfaceTexture(s);
        tex = nullptr;
    }

    bool fresh = tex == nullptr;

    if (!tex) {
        FrameResource* frame = frameCreate();

        tex = frame->make<SurfaceTexture>();
        tex->arenaOwned = true;
        tex->w = s.width;
        tex->h = s.height;
        s.frame = frame;

        try {
            createImage(s.width, s.height, kVkFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, tex->image, tex->memory);
            createHostBuffer((VkDeviceSize)s.width * s.height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tex->staging, tex->stagingMemory, &tex->stagingMap);
        } catch (...) {
            sysE << "imway: texture allocation failed "_sv << s.width << "x"_sv << s.height << endL;
            s.frame = nullptr;
            frameUnref(frame);

            return;
        }

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

        vci.image = tex->image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = kVkFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vci, nullptr, &tex->view);
        tex->ds = ImGui_ImplVulkan_AddTexture(sampler, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        tex->lease = frame->make<TextureLease>(this, tex, [](void* owner, SurfaceTexture* texture) {
            ((RendererImpl*)owner)->destroyTexture(texture);
        });
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
    FrameResource* frame = s.frame;

    s.texture = nullptr;

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
    if (forceComposition || !scene->popups.empty() || scene->dragIcon) {
        return nullptr;
    }

    // any open compositor ui needs composition
    if (launcherState || calendarState || wifiState || inspectorState || historyState || pickShow || pickArmed || pickPending ||
        altTabActive || osdMs || settings.open) {
        return nullptr;
    }

    if (scene->drawCursor) {
        Surface* cursor = scene->cursorSurface;

        if (!hwCursorReady || output->cursorCapW() <= 0 || (cursor &&
            (cursor->dmabuf || cursor->width > output->cursorCapW() || cursor->height > output->cursorCapH()))) {
            return nullptr;
        }
    }

    // an active toast must be composited to show
    if (notifier) {
        bool any = false;

        notifier->active([&](Toast&) {
            any = true;
        });

        if (any) {
            return nullptr;
        }
    }

    Toplevel* fs = nullptr;
    int mapped = 0;

    forEach<Toplevel>(scene->toplevels, [&](Toplevel& t) {
        if (t.mapped) {
            mapped++;

            if (t.fullscreen) {
                fs = &t;
            }
        }
    });

    if (!fs || mapped != 1) {
        return nullptr;
    }

    Surface* s = fs->surface;

    if (!s || !s->dmabuf || !s->hasContent || s->explicitSync || s->bufferTransform != 0 || s->bufferScale != 1 ||
        s->bufferOffsetX != 0 || s->bufferOffsetY != 0 || s->vp.hasSrc || s->vp.hasDst) {
        return nullptr;
    }

    if (!s->stackBelow.empty() || !s->stackAbove.empty()) {
        return nullptr; // subsurfaces need composition
    }

    if (s->geomW() != scene->outW || s->geomH() != scene->outH) {
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

    if (tex->lease) {
        tex->lease->texture = nullptr;
        tex->lease = nullptr;
    }

    if (tex->ds) {
        ImGui_ImplVulkan_RemoveTexture(tex->ds);
    }

    freeConversion(tex);

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

    tex->unlink();

    if (!tex->arenaOwned) {
        textureAlloc.release(tex);
    }
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

    auto* tex = b->lifetime->make<SurfaceTexture>();

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

        if (i > 0 && (!firstStatOk || fstat(b->fds[i], &planeStat) != 0 ||
                      planeStat.st_dev != firstStat.st_dev || planeStat.st_ino != firstStat.st_ino)) {
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

    tex->ds = ImGui_ImplVulkan_AddTexture(sampler, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    tex->lease = b->lifetime->make<TextureLease>(this, tex, [](void* owner, SurfaceTexture* texture) {
        ((RendererImpl*)owner)->destroyTexture(texture);
    });
    textures.pushBack(tex);
    dmabufCache.pushBack({b, tex});
    s.texture = tex;

    return true;
}

ImVec2 transformedUv(int transform, float x, float y) {
    switch (transform) {
        case 1: return ImVec2(1.f - y, x);       // 90
        case 2: return ImVec2(1.f - x, 1.f - y); // 180
        case 3: return ImVec2(y, 1.f - x);       // 270
        case 4: return ImVec2(1.f - x, y);       // flipped
        case 5: return ImVec2(1.f - y, 1.f - x); // flipped 90
        case 6: return ImVec2(x, 1.f - y);       // flipped 180
        case 7: return ImVec2(y, x);             // flipped 270
        default: return ImVec2(x, y);
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

        if (s.bufferTransform == 0) {
            ImGui::Image((ImTextureID)(uintptr_t)s.texture->ds, ImVec2(w, h), uv[0], uv[2]);
        } else {
            ImGui::PushID(&s);
            ImGui::InvisibleButton("surface", ImVec2(w, h));
            ImGui::GetWindowDrawList()->AddImageQuad((ImTextureID)(uintptr_t)s.texture->ds,
                ImVec2(x, y), ImVec2(x + w, y), ImVec2(x + w, y + h), ImVec2(x, y + h), uv[0], uv[1], uv[2], uv[3]);
            ImGui::PopID();
        }

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
        ImGui::GetForegroundDrawList()->AddImageQuad((ImTextureID)(uintptr_t)s.texture->ds,
            ImVec2(x, y), ImVec2(x + w, y), ImVec2(x + w, y + h), ImVec2(x, y + h), uv[0], uv[1], uv[2], uv[3]);
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

void RendererImpl::markTreeUnhovered(Surface& s) {
    s.hovered = false;

    forEach<Subsurface>(s.stackBelow, [&](Subsurface& c) {
        if (c.surface) {
            markTreeUnhovered(*c.surface);
        }
    });

    forEach<Subsurface>(s.stackAbove, [&](Subsurface& c) {
        if (c.surface) {
            markTreeUnhovered(*c.surface);
        }
    });
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
                cursorPoly(dl, ns, 10, p, s, R, R);
                return;
            case ImGuiMouseCursor_ResizeNWSE:
                cursorPoly(dl, ns, 10, p, s, R, -R);
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
    // the docking backend dereferences the owner viewport for its render
    // buffers; the hand-built draw data must point at the main one
    dd.OwnerViewport = ImGui::GetMainViewport();

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

    if (VkResult res = vkQueueSubmit(queue, 1, &si, curFence); res != VK_SUCCESS) {
        // out is pre-zeroed by the caller: a transparent cursor beats
        // freezing the whole session in an infinite fence wait
        sysE << "imway: cursor rasterize submit failed ("_sv << (long)res << ")"_sv << endL;

        return;
    }

    vkWaitForFences(device, 1, &curFence, VK_TRUE, UINT64_MAX);
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

namespace {
    StringView readSmallFile(Buffer& path, Buffer& out) {
        out.reset();
        readFileContent(path, out);

        return sv(out);
    }

    // "MemAvailable:    1234 kB" -> 1234
    long meminfoKb(StringView text, StringView key) {
        StringView rest = text;

        while (!rest.empty()) {
            StringView line, tail;

            if (!rest.split('\n', line, tail)) {
                line = rest;
                tail = {};
            }

            rest = tail;

            if (line.startsWith(key)) {
                long v = 0;

                for (u8 c : line) {
                    if (c >= '0' && c <= '9') {
                        v = v * 10 + (c - '0');
                    }
                }

                return v;
            }
        }

        return 0;
    }
}

void RendererImpl::sampleStats() {
    u64 now = nowMsec();

    if (statMs && now - statMs < 1900) {
        return;
    }

    statMs = now;

    Buffer content;

    // cpu: busy/total delta over the first /proc/stat line
    if (Buffer path("/proc/stat"_sv); !readSmallFile(path, content).empty()) {
        StringView st = sv(content);
        u64 vals[8] = {};
        int n = 0;
        u64 cur = 0;
        bool in = false;

        for (u8 c : st) {
            if (c == '\n') {
                break;
            }

            if (c >= '0' && c <= '9') {
                cur = cur * 10 + (c - '0');
                in = true;
            } else if (in) {
                if (n < 8) {
                    vals[n++] = cur;
                }

                cur = 0;
                in = false;
            }
        }

        u64 total = 0;

        for (int i = 0; i < n; i++) {
            total += vals[i];
        }

        u64 busy = total - vals[3] - vals[4]; // minus idle, iowait

        if (cpuPrevTotal && total > cpuPrevTotal) {
            cpuPct = (int)((busy - cpuPrevBusy) * 100 / (total - cpuPrevTotal));
        }

        cpuPrevBusy = busy;
        cpuPrevTotal = total;
    }

    if (Buffer path("/proc/meminfo"_sv); !readSmallFile(path, content).empty()) {
        long total = meminfoKb(sv(content), "MemTotal:"_sv);
        long avail = meminfoKb(sv(content), "MemAvailable:"_sv);

        memUsedMb = (total - avail) / 1024;
    }

    // no battery picked (startup, or the last one vanished): re-enumerate.
    // system batteries win over scope=Device ones (hid keyboards/mice) —
    // those ride usb hotplug, so they are a fallback, not the first choice
    if (batPath.empty()) {
        StringBuilder devBat;

        try {
            listDir("/sys/class/power_supply"_sv, [this, &content, &devBat](const TPathInfo& e) {
                if (!batPath.empty()) {
                    return;
                }

                StringBuilder p;

                p << "/sys/class/power_supply/"_sv << e.item << "/type"_sv;

                if (!readSmallFile(p, content).startsWith("Battery"_sv)) {
                    return;
                }

                bool device = false;

                try {
                    p.reset();
                    p << "/sys/class/power_supply/"_sv << e.item << "/scope"_sv;
                    device = readSmallFile(p, content).startsWith("Device"_sv);
                } catch (...) {
                    // no scope file: a system battery
                }

                if (device) {
                    if (devBat.empty()) {
                        devBat << "/sys/class/power_supply/"_sv << e.item;
                    }
                } else {
                    batPath << "/sys/class/power_supply/"_sv << e.item;
                }
            });
        } catch (...) {
        }

        if (batPath.empty() && !devBat.empty()) {
            batPath << sv(devBat);
        }
    }

    batPct = -1;

    if (!batPath.empty()) {
        try {
            auto& p = sb();

            p << sv(batPath) << "/capacity"_sv;
            batPct = (long)readSmallFile(p, content).stou();
            p.reset();
            p << sv(batPath) << "/status"_sv;
            batCharging = readSmallFile(p, content).startsWith("Charging"_sv);
        } catch (...) {
            // the supply vanished under us (hub unplugged): forget it, the
            // next tick re-enumerates — and finds it again on replug
            batPath.reset();
        }
    }
}

// resizeAnchor bits: which edge stays under the hand during a drag
enum { kResizeLeft = 1, kResizeTop = 2, kResizeActive = 0x80 };

// transactional resize: a border/grip drag is a request, not a resize. the
// callback pins the window at the client's committed size (applyW/H) and
// records where the hand wants it (dragW/H) — that becomes a configure, and
// the window steps only when the client commits a matching buffer. anything
// that is not an active border/grip drag (initial sizing, our own applies)
// passes through untouched. it also records which edge is being dragged so the
// draw loop can grow toward the hand for left/top drags
static void toplevelSizeCb(ImGuiSizeCallbackData* d) {
    auto* t = (Toplevel*)d->UserData;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* w = g.CurrentWindow;

    if (!g.ActiveId || g.ActiveIdWindow != w) {
        return;
    }

    bool resizing = false;
    u8 anchor = 0;

    if (g.ActiveId == ImGui::GetWindowResizeBorderID(w, ImGuiDir_Left)) {
        resizing = true;
        anchor |= kResizeLeft;
    }

    if (g.ActiveId == ImGui::GetWindowResizeBorderID(w, ImGuiDir_Up)) {
        resizing = true;
        anchor |= kResizeTop;
    }

    if (g.ActiveId == ImGui::GetWindowResizeBorderID(w, ImGuiDir_Right) || g.ActiveId == ImGui::GetWindowResizeBorderID(w, ImGuiDir_Down)) {
        resizing = true;
    }

    // corner grips, in imgui's order: 0 bottom-right, 1 bottom-left, 2 top-left,
    // 3 top-right
    if (g.ActiveId == ImGui::GetWindowResizeCornerID(w, 0)) {
        resizing = true;
    }

    if (g.ActiveId == ImGui::GetWindowResizeCornerID(w, 1)) {
        resizing = true;
        anchor |= kResizeLeft;
    }

    if (g.ActiveId == ImGui::GetWindowResizeCornerID(w, 2)) {
        resizing = true;
        anchor |= kResizeLeft | kResizeTop;
    }

    if (g.ActiveId == ImGui::GetWindowResizeCornerID(w, 3)) {
        resizing = true;
        anchor |= kResizeTop;
    }

    if (!resizing) {
        return;
    }

    t->dragW = d->DesiredSize.x;
    t->dragH = d->DesiredSize.y;
    d->DesiredSize = ImVec2(t->applyW, t->applyH);

    // latch the anchor for the whole transaction (drag through client commit)
    if (!(t->resizeAnchor & kResizeActive)) {
        t->resizeAnchor = anchor | kResizeActive;
    }
}

static void spawnClient(StringView cmd, StringView sock) {
    if (cmd.empty() || sock.empty()) {
        return;
    }

    // materialize before the fork, both live until the exec
    Buffer c(cmd), s(sock);
    pid_t pid = fork();

    if (pid == 0) {
        // double fork: the command reparents to init, no zombies
        if (fork() != 0) {
            _exit(0);
        }

        setenv("WAYLAND_DISPLAY", s.cStr(), 1);
        execlp("sh", "sh", "-c", c.cStr(), (char*)nullptr);
        _exit(127);
    }

    if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

static StringView wifiGlyph(WifiState s) {
    switch (s) {
        case WifiState::connected: return "wifi"_sv;
        case WifiState::connecting: return "wifi..."_sv;
        case WifiState::scanning: return "wifi.."_sv;
        case WifiState::disconnected: return "wifi off"_sv;
        case WifiState::unavailable: return "no wifi"_sv;
    }

    return "wifi"_sv;
}

void RendererImpl::buildUi(Scene& scene) {
    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = (float)(1.0 / scene.hz);

    // the ui only writes nextUiScale: the scale flips here and nowhere
    // else, so a whole frame never mixes the new scale with the old style
    if (nextUiScale != uiScale) {
        uiScale = nextUiScale;

        // restyle from a pristine copy: ScaleAllSizes compounds otherwise
        ImGuiStyle fresh;

        fresh.FontScaleMain = uiScale;
        fresh.ScaleAllSizes(uiScale);
        ImGui::GetStyle() = fresh;

        shadow.scale = uiScale;

        // hw cursor bitmaps bake the scale in: drop and re-rasterize
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

    scene.focusedToplevel = nullptr;

    if (ImGui::BeginMainMenuBar()) {
        if (settings.sdrNits < 0.f) {
            settings.sdrNits = (float)output->sdrWhiteNits();
        }

        settings.uiScale = uiScale;

        if (comp->mixer) {
            settings.volume = comp->mixer->volume();
            settings.volMuted = comp->mixer->muted();
        } else {
            settings.volume = -1.f;
        }

        settings.brightness = output->hasBrightness() ? output->brightness() : -1.f;

        if (notifier) {
            settings.hasDnd = true;
            settings.dnd = notifier->dnd();
        } else {
            settings.hasDnd = false;
        }

        drawSettingsMenu(settings);

        if (settings.dndChanged && notifier) {
            notifier->setDnd(settings.dnd);
        }

        if (settings.volumeChanged && comp->mixer) {
            comp->mixer->setVolume(settings.volume);
        }

        if (settings.muteChanged && comp->mixer) {
            comp->mixer->setMuted(settings.volMuted);
        }

        if (settings.brightnessChanged) {
            output->setBrightness(settings.brightness);
        }

        if (settings.scaleChanged) {
            nextUiScale = settings.scale;
        }

        if (settings.sdrChanged) {
            output->setSdrWhite(settings.sdrNits);
        }

        if (settings.nightChanged) {
            output->setColorTemp(settings.nightOn ? settings.nightK : 0);
        }

        if (settings.changed()) {
            scene.needsFrame = true;
        }

        time_t now = time(nullptr);
        tm lt{};

        localtime_r(&now, &lt);

        auto& clock = sb();
        auto pad2 = [&clock](int v) {
            if (v < 10) {
                clock << 0;
            }

            clock << v;
        };

        pad2(lt.tm_mday);
        clock << "."_sv;
        pad2(lt.tm_mon + 1);
        clock << " "_sv;
        pad2(lt.tm_hour);
        clock << ":"_sv;
        pad2(lt.tm_min);

        const ImGuiStyle& st = ImGui::GetStyle();
        float cw = ImGui::CalcTextSize(clock.cStr()).x;
        float x = ImGui::GetWindowWidth() - cw - st.ItemSpacing.x;

        ImGui::SameLine(x);
        ImGui::TextUnformatted(clock.cStr());

        if (ImGui::IsItemClicked()) {
            calendarToggle = true;
        }

        float xl = x;

        if (scene.layout[0]) {
            float lw = ImGui::CalcTextSize(scene.layout).x;

            xl = x - lw - st.ItemSpacing.x * 2;
            ImGui::SameLine(xl);
            ImGui::TextUnformatted(scene.layout);
        }

        // clock is on screen, the shared builder is free again
        sampleStats();

        auto& stat = sb();

        stat << "cpu "_sv << cpuPct << "%  "_sv << memUsedMb / 1024 << "."_sv << memUsedMb % 1024 * 10 / 1024 << "G"_sv;

        if (batPct >= 0) {
            stat << "  bat "_sv << batPct << "%"_sv;

            if (batCharging) {
                stat << "+"_sv;
            }
        }

        float sw = ImGui::CalcTextSize(stat.cStr()).x;
        float xs = xl - sw - st.ItemSpacing.x * 2;

        ImGui::SameLine(xs);
        ImGui::TextUnformatted(stat.cStr());

        if (comp->wifi) {
            StringView wl = wifiGlyph(comp->wifi->state());
            float ww = ImGui::CalcTextSize((const char*)wl.begin(), (const char*)wl.end()).x;
            float xw = xs - ww - st.ItemSpacing.x * 2;

            ImGui::SameLine(xw);
            ImGui::TextUnformatted((const char*)wl.begin(), (const char*)wl.end());

            if (ImGui::IsItemClicked()) {
                wifiToggle = true;
            }
        }

        ImGui::EndMainMenuBar();
    }

    drawCalendar(*comp, calendarToggle, &calendarState);
    calendarToggle = false;

    if (comp->wifi) {
        drawWifi(*comp, wifiToggle, &wifiState);
        wifiToggle = false;
    }

    if (notifier) {
        drawToasts(*notifier, *icons, *this, width, uiScale);
    }

    if (osdMs) {
        u64 now = nowMsec();

        if (now >= osdMs) {
            osdMs = 0;
        } else {
            float rem = (float)(osdMs - now) / 1000.f;
            float alpha = rem > 0.3f ? 1.f : rem / 0.3f;

            if (osdKind == 1 && comp->mixer) {
                drawOsd(width, uiScale, "volume"_sv, comp->mixer->volume(), comp->mixer->muted(), alpha);
            } else if (osdKind == 2) {
                drawOsd(width, uiScale, "brightness"_sv, output->brightness(), false, alpha);
            }

            scene.needsFrame = true;
        }
    }

    {
        InspectorInfo info;

        info.frameMs = frameMs;
        info.frameIdx = frameMsIdx;
        info.textures = textures.length();
        info.dmabufCache = dmabufCache.length();
        info.hwCursorKind = hwKind;
        info.hwCursorVisible = hwVisible;
        drawInspector(*comp, info, inspectorToggle, &inspectorState);
        inspectorToggle = false;
    }

    if (notifier) {
        drawHistory(*comp, historyToggle, &historyState);
        historyToggle = false;
    }

    if (pickShow) {
        static const char hx[] = "0123456789abcdef";
        char h[8] = {'#', hx[pickR >> 4], hx[pickR & 15], hx[pickG >> 4], hx[pickG & 15], hx[pickB >> 4], hx[pickB & 15], 0};

        ImGui::SetNextWindowPos(ImVec2((float)width / 2.f, (float)height / 2.f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::Begin("##pick", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
            float sz = ImGui::GetFontSize() * 2.4f;
            ImVec2 p = ImGui::GetCursorScreenPos();

            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(pickR, pickG, pickB, 255));
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(180, 180, 190, 255));
            ImGui::Dummy(ImVec2(sz, sz));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(h);

            if (ImGui::SmallButton("copy")) {
                ImGui::SetClipboardText(h);
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                pickShow = false;
            }

            ImGui::EndGroup();
        }

        ImGui::End();
        scene.needsFrame = true;
    }

    // client frames are half-width
    const ImVec2 fullPad = ImGui::GetStyle().WindowPadding;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fullPad.x * 0.5f, fullPad.y * 0.5f));

    int i = 0;

    forEach<Toplevel>(scene.toplevels, [&](Toplevel& value) {
        Toplevel* t = &value;
        Surface* root = t->surface;

        if (!t->mapped || !root || !root->texture) {
            if (root) {
                markTreeUnhovered(*root);
            }

            return;
        }

        StringView title = sv(t->title);

        if (title.length() > 280) {
            title = title.prefix(280);
        }

        auto& label = sb();

        label << title << "###toplevel"_sv << (u64)t->id;
        ImGui::SetNextWindowPos(ImVec2(40.f + 30.f * i, 60.f + 30.f * i), ImGuiCond_FirstUseEver);
        i++;

        const ImGuiStyle& st = ImGui::GetStyle();
        float header = t->csd ? 0.f : ImGui::GetFrameHeight();
        float chromeW = st.WindowPadding.x * 2;
        float chromeH = st.WindowPadding.y * 2 + header;

        t->applyW = (float)root->geomW() + chromeW;
        t->applyH = (float)root->geomH() + chromeH;

        // how much the applied size steps this frame — drives both the left/top
        // position compensation below and the end-of-transaction detection
        float stepW = t->applyW - t->lastApplyW;
        float stepH = t->applyH - t->lastApplyH;

        t->lastApplyW = t->applyW;
        t->lastApplyH = t->applyH;

        if (!t->docked && !t->fullscreen) {
            // when the size steps to a client-committed buffer during a left/top
            // drag, move the top-left by the same delta so the opposite edge
            // stays put and the window grows toward the hand
            if (t->resizeAnchor & kResizeActive) {
                float nx = t->curX - ((t->resizeAnchor & kResizeLeft) ? stepW : 0.f);
                float ny = t->curY - ((t->resizeAnchor & kResizeTop) ? stepH : 0.f);

                if (nx != t->curX || ny != t->curY) {
                    ImGui::SetNextWindowPos(ImVec2(nx, ny), ImGuiCond_Always);
                }
            }

            // the window is a function of the client's committed geometry:
            // a border drag never resizes it directly (the constraint
            // callback pins it), it only asks — the size steps here, when
            // the geometry answers; initial map sizing and the return from
            // fullscreen are the same rule
            ImGui::SetNextWindowSize(ImVec2(t->applyW, t->applyH), ImGuiCond_Always);
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(FLT_MAX, FLT_MAX), toplevelSizeCb, t);
        }

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        // csd clients (gtk) bring their own header bar; ours would double it.
        // without a title bar imgui exempts the window from
        // move-from-titlebar-only, so any in-content drag would move the
        // window instead of reaching the client — NoMove, lifted for the one
        // frame that serves a client-requested move (the move persists,
        // imgui does not re-check the flag mid-drag)
        if (t->csd) {
            flags |= ImGuiWindowFlags_NoTitleBar;

            if (!t->moveRequested) {
                flags |= ImGuiWindowFlags_NoMove;
            }
        }

        if (t->fullscreen) {
            // NoDocking alone does not pull a docked window out of its node
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2((float)width, (float)height), ImGuiCond_Always);
            flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;

            // the content must cover the output edge to edge: window padding
            // would carve an 8px gutter and shrink the configured size
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        }

        bool stayOpen = true;

        if (ImGui::Begin(label.cStr(), t->csd ? nullptr : &stayOpen, flags)) {
            t->docked = ImGui::IsWindowDocked();

            // remember imgui's truth of the position, the base for next frame's
            // left/top resize compensation
            ImVec2 wp = ImGui::GetWindowPos();

            t->curX = wp.x;
            t->curY = wp.y;

            if (ImGui::IsWindowFocused()) {
                scene.focusedToplevel = t;
            }

            if (t->raiseRequested) {
                t->raiseRequested = false;
                ImGui::SetWindowFocus();
            }

            if (t->moveRequested) {
                if (!t->fullscreen && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ImGuiWindow* window = ImGui::GetCurrentWindow();

                    ImGui::StartMouseMovingWindowOrNode(window, window->DockNode, true);
                }

                t->moveRequested = false;
            }

            if (t->resizeEdges) {
                if (!t->docked && !t->fullscreen && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ImVec2 mouse = ImGui::GetMousePos();

                    t->clientResizeEdges = t->resizeEdges;
                    t->resizeStartMouseX = mouse.x;
                    t->resizeStartMouseY = mouse.y;
                    t->resizeStartW = t->applyW;
                    t->resizeStartH = t->applyH;
                    t->resizeAnchor = kResizeActive |
                                      ((t->resizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_LEFT) ? kResizeLeft : 0) |
                                      ((t->resizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_TOP) ? kResizeTop : 0);
                }

                t->resizeEdges = 0;
            }

            if (t->clientResizeEdges) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ImVec2 mouse = ImGui::GetMousePos();
                    float dx = mouse.x - t->resizeStartMouseX;
                    float dy = mouse.y - t->resizeStartMouseY;
                    float rw = (t->clientResizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_RIGHT) ? dx
                             : (t->clientResizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_LEFT) ? -dx : 0.f;
                    float rh = (t->clientResizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM) ? dy
                             : (t->clientResizeEdges & XDG_TOPLEVEL_RESIZE_EDGE_TOP) ? -dy : 0.f;

                    t->dragW = t->resizeStartW + rw;
                    t->dragH = t->resizeStartH + rh;

                    if (t->dragW < chromeW + 1.f) {
                        t->dragW = chromeW + 1.f;
                    }

                    if (t->dragH < chromeH + 1.f) {
                        t->dragH = chromeH + 1.f;
                    }
                } else {
                    t->clientResizeEdges = 0;
                }
            }

            if (t->docked || t->fullscreen) {
                // the node/screen dictates the size, the client must fill it
                ImVec2 avail = ImGui::GetContentRegionAvail();

                t->desiredW = (int)avail.x;
                t->desiredH = (int)avail.y;
            } else if (t->dragW > 0.f) {
                // floating drag: the request, in client pixels
                t->desiredW = (int)(t->dragW - chromeW);
                t->desiredH = (int)(t->dragH - chromeH);
            } else {
                // steady state: ask for what the client already has, i.e.
                // nothing — floating configures originate from drags only
                t->desiredW = root->geomW();
                t->desiredH = root->geomH();

                // no active drag and the size has stopped changing: the resize
                // transaction is done, stop compensating so a move can proceed
                if ((t->resizeAnchor & kResizeActive) && stepW == 0.f && stepH == 0.f) {
                    t->resizeAnchor = 0;
                }
            }

            if (!t->docked && !t->fullscreen) {
                if (t->minW > 0 && t->desiredW < t->minW) {
                    t->desiredW = t->minW;
                }

                if (t->minH > 0 && t->desiredH < t->minH) {
                    t->desiredH = t->minH;
                }

                if (t->maxW > 0 && t->desiredW > t->maxW) {
                    t->desiredW = t->maxW;
                }

                if (t->maxH > 0 && t->desiredH > t->maxH) {
                    t->desiredH = t->maxH;
                }
            }

            t->dragW = 0.f;
            t->dragH = 0.f;

            ImVec2 origin = ImGui::GetCursorScreenPos();

            drawSurfaceTree(*root, origin.x, origin.y);

        } else {
            markTreeUnhovered(*root);
        }

        ImGui::End();

        if (t->fullscreen) {
            ImGui::PopStyleVar(2);
        }

        if (!stayOpen) {
            t->closeRequested = true;
        }
    });

    ImGui::PopStyleVar();

    forEach<Popup>(scene.popups, [&](Popup& p) {
        Surface* ps = p.surface;

        if (!p.mapped || !ps || !ps->texture || !p.parent) {
            if (ps) {
                markTreeUnhovered(*ps);
            }
        } else {
            drawSurfaceTreeOverlay(*ps, p.parent->imgX + (float)p.parent->geomX() + (float)p.x, p.parent->imgY + (float)p.parent->geomY() + (float)p.y);
        }
    });

    if (scene.dragIcon && scene.dragIcon->texture) {
        ImVec2 mp = ImGui::GetMousePos();

        drawSurfaceTreeOverlay(*scene.dragIcon, mp.x + 4, mp.y + 4);
    }

    bool overClient = false;

    forEach<Surface, SceneNode>(scene.surfaces, [&](Surface& s) {
        // decoration surfaces (cursor image, drag icon) ride the pointer:
        // their hover flags go stale the moment they stop being drawn and
        // would pin this true forever; contentless surfaces likewise keep
        // the flag from their last drawn frame
        if (&s != scene.cursorSurface && &s != scene.dragIcon && s.hovered && s.hasContent) {
            overClient = true;
        }
    });

    if (altTabActive && !intrListContains<Toplevel>(scene.toplevels, altTabSel)) {
        // the selected window died under the overlay
        altTabActive = false;
        altTabSel = nullptr;
    }

    if (launcherState || launcherToggle) {
        // the dialog is on screen this frame or flipping — one more frame
        // settles either way
        scene.needsFrame = true;
    }

    {
        Buffer cmd;
        LauncherAction act = LauncherAction::none;

        if (drawLauncher(*comp, launcherToggle, &launcherState, cmd, act)) {
            switch (act) {
                case LauncherAction::notifications:
                    historyToggle = true;
                    break;
                case LauncherAction::inspector:
                    inspectorToggle = true;
                    break;
                case LauncherAction::colorPicker:
                    pickArmed = true;
                    break;
                case LauncherAction::none:
                    spawnClient(sv(cmd), scene.socketName);
                    break;
            }
        }

        launcherToggle = false;
    }

    // ui owns the pointer when it is over our widgets but not over client
    // content (client windows are imgui windows too, hence the second term)
    scene.ptrCaptured = ImGui::GetIO().WantCaptureMouse && !overClient;

    // everything below draws into the foreground list, which sits on top of
    // all windows anyway — and it MUST happen before ImGui::Render(): draw
    // data totals are snapshotted there, late commands are silently dropped
    if (altTabActive) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        float th = 120.f * uiScale;
        float pad = 12.f * uiScale;
        float total = pad;
        int count = 0;

        forEach<Toplevel>(scene.toplevels, [&](Toplevel& value) {
            Toplevel* t = &value;

            if (!t->mapped || !t->surface || !t->surface->texture) {
                return;
            }

            float sw = (float)t->surface->geomW(), sh = (float)t->surface->geomH();
            float tw = sh > 0.f ? th * sw / sh : th;

            total += (tw > th * 2.f ? th * 2.f : tw) + pad;
            count++;
        });

        if (count) {
            float lineH = ImGui::GetFontSize();
            float boxH = th + lineH + pad * 3.f;
            float x = ((float)width - total) / 2.f;
            float y0 = ((float)height - boxH) / 2.f;

            dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + total, y0 + boxH), IM_COL32(18, 18, 24, 235), 8.f * uiScale);
            x += pad;

            forEach<Toplevel>(scene.toplevels, [&](Toplevel& value) {
                Toplevel* t = &value;

                if (!t->mapped || !t->surface || !t->surface->texture) {
                    return;
                }

                float sw = (float)t->surface->geomW(), sh = (float)t->surface->geomH();
                float tw = sh > 0.f ? th * sw / sh : th;

                if (tw > th * 2.f) {
                    tw = th * 2.f;
                }

                float y = y0 + pad;
                float texW = (float)t->surface->texture->w, texH = (float)t->surface->texture->h;
                ImVec2 tuv0((float)t->surface->geomX() / texW, (float)t->surface->geomY() / texH);
                ImVec2 tuv1(((float)t->surface->geomX() + sw) / texW, ((float)t->surface->geomY() + sh) / texH);

                dl->AddImage((ImTextureID)(uintptr_t)t->surface->texture->ds, ImVec2(x, y), ImVec2(x + tw, y + th), tuv0, tuv1);

                if (t == altTabSel) {
                    dl->AddRect(ImVec2(x - 2.f, y - 2.f), ImVec2(x + tw + 2.f, y + th + 2.f), IM_COL32(255, 200, 60, 255), 0.f, 0, 3.f);
                }

                if (u64 tabIcon = iconTexture(t->icon)) {
                    float isz = 20.f * uiScale;

                    dl->AddImage((ImTextureID)tabIcon, ImVec2(x + 3.f, y + 3.f), ImVec2(x + 3.f + isz, y + 3.f + isz));
                }

                StringView title = sv(t->title);

                if (title.length() > 24) {
                    title = title.prefix(24);
                }

                auto& lbl = sb();

                lbl << title;
                dl->AddText(ImVec2(x, y + th + pad * 0.75f), IM_COL32(230, 230, 230, 255), lbl.cStr());
                x += tw + pad;
            });
        }
    }

    cursorUi(scene, overClient);
    ImGui::Render();
}

void RendererImpl::cursorUi(Scene& scene, bool overClient) {
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
        // client-provided cursor surface: feed its pixels to the cursor plane.
        // The plane copies raw buffer pixels, so a scaled/transformed/viewport
        // cursor (HiDPI themes) must fall back to composition or it shows up
        // double-size with a misplaced hotspot
        bool plainBuffer = cs->bufferScale == 1 && cs->bufferTransform == 0 && !cs->vp.hasSrc && !cs->vp.hasDst;
        bool hwOk = hwCursor && !cs->dmabuf && plainBuffer && cs->width > 0 && cs->height > 0 && cs->width <= hwCapW && cs->height <= hwCapH && cs->pixels.length() >= (size_t)cs->width * cs->height * 4;

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

        if (getenv("IMWAY_DEBUG_CURSOR") && scene.framesDone % 120 == 0) {
            sysE << "cursor dbg: kind "_sv << kind << ", mp "_sv << mp.x << ","_sv << mp.y << ", overClient "_sv << (int)overClient << ", cs "_sv << (int)(cs != nullptr) << ", shape "_sv << (int)scene.cursorShape << endL;
        }

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
            // rasterizing goes through the imgui vulkan backend, which must
            // not run mid-frame (on the very first frame it is not even
            // initialized yet and produces an empty image): defer to the
            // end of renderFrame
            pendingShape = kind;
        } else {
            output->setCursorImage(img.data());
            hwKind = kind;
            hwSurf = nullptr;
        }
    }

    hwHotX = hwCapW / 2;
    hwHotY = hwCapH / 2;
    hwVisible = true;
    output->setCursorPos((int)mp.x - hwHotX, (int)mp.y - hwHotY, true);
}

bool RendererImpl::renderFrame(int scanIdx) {
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
                    // the protocol lets the client commit before attaching a
                    // fence to the acquire point; WAIT_FOR_SUBMIT makes the
                    // transfer wait for materialization instead of failing
                    u32 binary = 0;
                    int syncFd = -1;
                    bool exported = drmSyncobjCreate(drmFd, 0, &binary) == 0 &&
                                    drmSyncobjTransfer(drmFd, binary, 0, s->syncAcquireHandle, s->syncAcquirePoint, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT) == 0 &&
                                    drmSyncobjExportSyncFile(drmFd, binary, &syncFd) == 0 && syncFd >= 0;

                    if (binary) {
                        drmSyncobjDestroy(drmFd, binary);
                    }

                    // still no fence: skip just this surface and retry next
                    // frame; failing the whole render would stall frame
                    // callbacks for every client
                    if (!exported) {
                        if (syncFd >= 0) {
                            close(syncFd);
                        }

                        scene->needsFrame = true;

                        return;
                    }

                    if (!waitOnSyncFile(syncFd)) {
                        scene->needsFrame = true;

                        return;
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

    buildUi(*scene);

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

    // color-managed surfaces: convert their (now uploaded) source into the
    // sRGB composition space before ImGui samples the converted texture
    forEach<Surface, SceneNode>(scene->surfaces, [&](Surface& s) {
        if (s.texture && s.texture->converted && surfaceVisible(&s)) {
            recordConversion(cmd, s.texture, s);
        }
    });

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
        sysE << "imway: Vulkan queue submit failed ("_sv << (long)submitResult << ")"_sv << endL;
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
            hwSurf = nullptr;
            scene->needsFrame = true;
        }
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
        sysE << "imway: readback submit failed ("_sv << (long)res << ")"_sv << endL;

        return false;
    }

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
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

// grab the composed frame into an anonymous memfd (RGBA8, self-describing
// header) and hand it to the crop tool via /proc/self/fd — no temp file,
// the buffer lives in ram and the kernel reclaims it when the tool exits
void RendererImpl::captureScreenshot() {
    if (lastFrameDirect) {
        forceComposition = true;
        scene->needsFrame = true;
        frameNow();
        forceComposition = false;
    }

    if (!haveFrame) {
        return;
    }

    finishGpuFrame(true);

    if (!readbackLastFrame()) {
        return;
    }

    // non-cloexec: the memfd must survive fork+exec so the tool can read it
    // through /proc/self/fd
    int mfd = memfd_create("imway-shot", 0);

    if (mfd < 0) {
        return;
    }

    // self-describing header: 'IMW1' magic + width + height, then RGBA8 rows
    struct {
        u32 magic;
        u32 w;
        u32 h;
    } hdr = {0x31574d49u, (u32)width, (u32)height};

    (void)!write(mfd, &hdr, sizeof(hdr));

    auto* px = (const unsigned char*)readbackMap;
    Vector<u8> row;

    row.zero((size_t)width * 4);

    for (int y = 0; y < height; y++) {
        const unsigned char* src = px + (size_t)y * width * 4;

        if (fmt == VK_FORMAT_A2R10G10B10_UNORM_PACK32) {
            const u32* p = (const u32*)src;

            for (int x = 0; x < width; x++) {
                row.mut(x * 4 + 0) = (u8)((p[x] >> 22) & 0xff);
                row.mut(x * 4 + 1) = (u8)((p[x] >> 12) & 0xff);
                row.mut(x * 4 + 2) = (u8)((p[x] >> 2) & 0xff);
                row.mut(x * 4 + 3) = 0xff;
            }
        } else {
            for (int x = 0; x < width; x++) {
                row.mut(x * 4 + 0) = src[x * 4 + 2];
                row.mut(x * 4 + 1) = src[x * 4 + 1];
                row.mut(x * 4 + 2) = src[x * 4 + 0];
                row.mut(x * 4 + 3) = 0xff;
            }
        }

        (void)!write(mfd, row.data(), row.length());
    }

    auto& proc = sb();

    proc << "/proc/self/fd/"_sv << mfd;

    Buffer arg(sv(proc)), sock(scene->socketName);
    // hand our ui scale down: the tool is a plain client and would otherwise
    // render its imgui at scale 1
    Buffer scale(sv(StringBuilder() << (long double)uiScale));
    pid_t pid = fork();

    if (pid == 0) {
        if (fork() != 0) {
            _exit(0);
        }

        setenv("WAYLAND_DISPLAY", sock.cStr(), 1);
        setenv("IMGUI_SCALE", scale.cStr(), 1);
        // re-exec this very binary as the crop tool; the memfd rides along
        execl("/proc/self/exe", "imway", "screenshot", arg.cStr(), (char*)nullptr);
        _exit(127);
    }

    if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }

    close(mfd);
}

void RendererImpl::frameNow() {
    if (!finishGpuFrame(false)) {
        scene->needsFrame = true;

        return;
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

    // point each surface's ImGui descriptor at a converted texture (or back to
    // the plain one) before buildUi captures it — runs only when the image
    // description changed, no-op for the common uncolor-managed case
    forEach<Surface, SceneNode>(scene->surfaces, [&](Surface& s) {
        if (s.texture) {
            ensureConversion(s.texture, s);
        }
    });

    Surface* cand = scanoutCandidate();
    bool direct = cand && output->directScanout(cand->dmabuf, cand->frame);

    lastFrameDirect = direct;

    if (!direct) {
        int idx = output->scanoutCount() > 0 ? output->acquire() : -1;

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

            output->presentImage(idx, presentFenceFd);

            if (presentFenceFd >= 0) {
                close(presentFenceFd);
                presentFenceFd = -1;
            }
        } else {
            output->present(output->presentNeedsPixels() ? readbackMap : nullptr);
        }
    }

    if (pickPending) {
        pickPending = false;

        if (readPixel(pickX, pickY, pickR, pickG, pickB)) {
            pickShow = true;

            if (notifier) {
                static const char hx[] = "0123456789abcdef";
                char h[8] = {'#', hx[pickR >> 4], hx[pickR & 15], hx[pickG >> 4], hx[pickG & 15], hx[pickB >> 4], hx[pickB & 15], 0};

                Post p;

                p.app = "color picker"_sv;
                p.summary = "color picked"_sv;
                p.body = StringView(h); // copied by post() before h dies
                notifier->post(p);
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
    // headless present() reports frameShown before the GPU is done, so the
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
