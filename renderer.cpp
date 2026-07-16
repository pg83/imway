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
#include "notifications.h"
#include "settings.h"
#include "output.h"
#include "scene.h"
#include "shadow.h"
#include "toast.h"
#include "util.h"

#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>

#include <linux/dma-buf.h>

#include <ev.h>
#include <linux/input-event-codes.h>

#include <vulkan/vulkan.h>
#include <xkbcommon/xkbcommon-keysyms.h>

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
    u32 mips = 1;
    bool needsUpload = false;
    bool firstUse = true;
    bool external = false;
};

#define VK_CHECK(x) STD_VERIFY((x) == VK_SUCCESS)

namespace {
    void frameTimerCb(struct ev_loop*, ev_timer* w, int);
    void prepareCb(struct ev_loop*, ev_prepare* w, int);
    void clockTimerCb(struct ev_loop*, ev_timer* w, int);


    struct RendererImpl: public Renderer, public InputSink, public IconResolver {
        InputSink* sink() override;


        void modsChanged() override;

        struct ev_loop* loop = nullptr;
        stl::ObjPool* pool = nullptr;
        Scene* scene = nullptr;
        ::Output* output = nullptr;
        FrameListener* listener = nullptr;
        ev_timer frameTimer{};
        ev_timer clockTimer{};
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

        ObjList<SurfaceTexture> textureAlloc;
        Vector<SurfaceTexture*> textures;

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

        Notifications* notes = nullptr;

        // inspector (Super+F12): opaque dialog handle owned here, state
        // lives in the widget; non-null = open
        void* inspectorState = nullptr;
        bool inspectorToggle = false;
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

        RendererImpl(Composer& comp, const DeviceVk& vk, StringView font, float scale, int limit);

        ~RendererImpl() noexcept;

        void tick();

        u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags props);
        void createImage(int w, int h, VkFormat format, VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem, u32 mips = 1);
        void createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf, VkDeviceMemory& mem, void** map);
        void setup(int w, int h);
        void shutdown() noexcept;

        void drawSurfaceTree(Surface& s, float x, float y);
        void drawSurfaceTreeOverlay(Surface& s, float x, float y);
        void markTreeUnhovered(Surface& s);
        void buildUi(Scene& scene);
        void sampleStats();
        void cursorUi(Scene& scene, bool overClient);
        void rasterizeShape(int kind, u32* out);

        void clampPos();

        // every event feeds imgui first; whatever the compositor ui did not
        // capture flows on to the wayland slave sink
        void motion(double x, double y) override;
        void button(u32 btn, bool pressed) override;
        void key(u32 code, bool pressed) override;
        void scroll(double dx, double dy) override;

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
        void drainDead();

        bool wantFrame() const;

        void frameNow();
        void renderFrame(int scanIdx);
        bool screenshot(StringView path) override;
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

InputSink* RendererImpl::sink() {
    return this;
}

void RendererImpl::modsChanged() {
}

RendererImpl::RendererImpl(Composer& comp, const DeviceVk& vk, StringView font, float scale, int limit)
    : loop(comp.loop)
    , keyboard(comp.kb)
    , next(comp.wayland->sink())
    , pool(comp.pool)
    , scene(comp.scene)
    , output(comp.output)
    , listener(comp.wayland->frameListener())
    , icons(comp.icons)
    , notes(comp.notes)
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
    setup(scene->outW, scene->outH);

    // before any input arrives the cursor sits at the screen center
    posX = scene->outW / 2.0;
    posY = scene->outH / 2.0;
    ImGui::GetIO().AddMousePosEvent((float)posX, (float)posY);

    if (output->vsynced()) {
        ev_prepare_init(&prep, prepareCb);
        prep.data = this;
        ev_prepare_start(loop, &prep);
    } else {
        ev_timer_init(&frameTimer, frameTimerCb, 0., 1.0 / scene->hz);
        frameTimer.data = this;
        ev_timer_start(loop, &frameTimer);
    }

    ev_timer_init(&clockTimer, clockTimerCb, 2., 2.);
    clockTimer.data = this;
    ev_timer_start(loop, &clockTimer);
}

RendererImpl::~RendererImpl() noexcept {
    if (output->vsynced()) {
        ev_prepare_stop(loop, &prep);
    } else {
        ev_timer_stop(loop, &frameTimer);
    }

    ev_timer_stop(loop, &clockTimer);
    shutdown();
}

void RendererImpl::clampPos() {
    double x0 = 0, y0 = 0, x1 = width - 1, y1 = height - 1;

    if (scene->pointerConfined) {
        x0 = scene->confineX0 > x0 ? scene->confineX0 : x0;
        y0 = scene->confineY0 > y0 ? scene->confineY0 : y0;
        x1 = scene->confineX1 < x1 ? scene->confineX1 : x1;
        y1 = scene->confineY1 < y1 ? scene->confineY1 : y1;
    }

    posX = posX < x0 ? x0 : posX > x1 ? x1 : posX;
    posY = posY < y0 ? y0 : posY > y1 ? y1 : posY;
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
    int imguiBtn = btn == BTN_LEFT ? 0 : btn == BTN_RIGHT ? 1 : 2;

    scene->needsFrame = true;
    ImGui::GetIO().AddMouseButtonEvent(imguiBtn, pressed);

    // presses over our ui stay ours; releases always go through, the
    // slave drops the ones whose press it never saw
    if (next && (!pressed || !scene->ptrCaptured)) {
        next->button(btn, pressed);
    }
}

void RendererImpl::scroll(double dx, double dy) {
    scene->needsFrame = true;
    ImGui::GetIO().AddMouseWheelEvent((float)-dx, (float)-dy);

    if (next && !scene->ptrCaptured) {
        next->scroll(dx, dy);
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

    if (altTabActive && !pressed && (code == KEY_LEFTALT || code == KEY_RIGHTALT)) {
        altTabActive = false;
        altTabSel = nullptr;
        scene->needsFrame = true;
    }

    // 1. compositor-global chords are sacred: consumed before anyone,
    // imgui included, matched on the group-0 keysym so they work in
    // any layout
    if (next && keyboard && code < 256) {
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
    // lives in the slave's modsChanged)
    bool capture = launcherState || altTabActive || io.WantCaptureKeyboard;

    scene->kbCaptured = capture;

    // presses under capture stay ours; releases always go through — the
    // slave drops the ones whose press the client never saw, and a release
    // lost to a capture window would leave the client with a stuck key
    if (!capture || !pressed) {
        next->key(code, pressed);
    }

    next->modsChanged();
}

bool RendererImpl::chordAction(u32 mask, u32 sym) {
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
    auto& tls = scene->toplevels;
    long n = (long)tls.length();

    if (!n) {
        return;
    }

    Toplevel* base = altTabActive && contains(tls, altTabSel) ? altTabSel : scene->focusedToplevel;
    long cur = base ? indexOf(tls, base) : -1;

    for (long step = 1; step <= n; step++) {
        Toplevel* t = tls[(size_t)(((cur + dir * step) % n + n) % n)];

        if (t->mapped) {
            altTabActive = true;
            altTabSel = t;
            scene->needsFrame = true;

            return;
        }
    }
}

void RendererImpl::altTabCommit() {
    if (contains(scene->toplevels, altTabSel) && altTabSel->mapped) {
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

        for (auto& sem : syncWaitPool) {
            VK_CHECK(vkCreateSemaphore(device, &plain, nullptr, &sem));
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
        tex = textureAlloc.make();
        tex->w = s.width;
        tex->h = s.height;

        try {
            createImage(s.width, s.height, kVkFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, tex->image, tex->memory);
            createHostBuffer((VkDeviceSize)s.width * s.height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tex->staging, tex->stagingMemory, &tex->stagingMap);
        } catch (...) {
            sysE << "imway: texture allocation failed "_sv << s.width << "x"_sv << s.height << endL;
            textureAlloc.release(tex);

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
    textureAlloc.release(tex);
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

    auto* tex = textureAlloc.make();

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
        textureAlloc.release(tex);

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

        textureAlloc.release(tex);

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

// x/y is where the VISIBLE part (window geometry) goes; imgX/imgY keep the
// screen position of the surface origin, so client coordinate math holds
void RendererImpl::drawSurfaceTree(Surface& s, float x, float y) {
    float gx = 0.f, gy = 0.f;
    float w = (float)s.viewW(), h = (float)s.viewH();
    ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
    bool viewported = s.vp.hasSrc || s.vp.hasDst;

    if (s.texture && s.vp.hasSrc && s.texture->w > 0 && s.texture->h > 0) {
        uv0 = ImVec2((float)(s.vp.sx / s.texture->w), (float)(s.vp.sy / s.texture->h));
        uv1 = ImVec2((float)((s.vp.sx + s.vp.sw) / s.texture->w), (float)((s.vp.sy + s.vp.sh) / s.texture->h));
    } else if (s.texture && !viewported && s.hasGeom && s.texture->w > 0 && s.texture->h > 0) {
        gx = (float)s.geomX();
        gy = (float)s.geomY();
        w = (float)s.geomW();
        h = (float)s.geomH();
        uv0 = ImVec2(gx / (float)s.texture->w, gy / (float)s.texture->h);
        uv1 = ImVec2((gx + w) / (float)s.texture->w, (gy + h) / (float)s.texture->h);
    }

    for (Subsurface* c : s.stackBelow) {
        if (c->surface && c->surface->hasContent) {
            drawSurfaceTree(*c->surface, x - gx + (float)c->x, y - gy + (float)c->y);
        }
    }

    if (s.texture) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::Image((ImTextureID)(uintptr_t)s.texture->ds, ImVec2(w, h), uv0, uv1);
        s.imgX = x - gx;
        s.imgY = y - gy;
        s.hovered = ImGui::IsItemHovered();
    }

    for (Subsurface* c : s.stackAbove) {
        if (c->surface && c->surface->hasContent) {
            drawSurfaceTree(*c->surface, x - gx + (float)c->x, y - gy + (float)c->y);
        }
    }
}

void RendererImpl::drawSurfaceTreeOverlay(Surface& s, float x, float y) {
    float gx = 0.f, gy = 0.f;
    float w = (float)s.viewW(), h = (float)s.viewH();
    ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
    bool viewported = s.vp.hasSrc || s.vp.hasDst;

    if (s.texture && s.vp.hasSrc && s.texture->w > 0 && s.texture->h > 0) {
        uv0 = ImVec2((float)(s.vp.sx / s.texture->w), (float)(s.vp.sy / s.texture->h));
        uv1 = ImVec2((float)((s.vp.sx + s.vp.sw) / s.texture->w), (float)((s.vp.sy + s.vp.sh) / s.texture->h));
    } else if (s.texture && !viewported && s.hasGeom && s.texture->w > 0 && s.texture->h > 0) {
        gx = (float)s.geomX();
        gy = (float)s.geomY();
        w = (float)s.geomW();
        h = (float)s.geomH();
        uv0 = ImVec2(gx / (float)s.texture->w, gy / (float)s.texture->h);
        uv1 = ImVec2((gx + w) / (float)s.texture->w, (gy + h) / (float)s.texture->h);
    }

    for (Subsurface* c : s.stackBelow) {
        if (c->surface && c->surface->hasContent) {
            drawSurfaceTreeOverlay(*c->surface, x - gx + (float)c->x, y - gy + (float)c->y);
        }
    }

    if (s.texture) {
        ImGui::GetForegroundDrawList()->AddImage((ImTextureID)(uintptr_t)s.texture->ds, ImVec2(x, y), ImVec2(x + w, y + h), uv0, uv1);
        s.imgX = x - gx;
        s.imgY = y - gy;

        ImVec2 m = ImGui::GetIO().MousePos;

        s.hovered = m.x >= x && m.y >= y && m.x < x + w && m.y < y + h;
    }

    for (Subsurface* c : s.stackAbove) {
        if (c->surface && c->surface->hasContent) {
            drawSurfaceTreeOverlay(*c->surface, x - gx + (float)c->x, y - gy + (float)c->y);
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
    vkQueueSubmit(queue, 1, &si, curFence);
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

// transactional resize: a border/grip drag is a request, not a resize. the
// callback pins the window at the client's committed size (applyW/H) and
// records where the hand wants it (dragW/H) — that becomes a configure, and
// the window steps only when the client commits a matching buffer. anything
// that is not an active border/grip drag (initial sizing, our own applies)
// passes through untouched
static void toplevelSizeCb(ImGuiSizeCallbackData* d) {
    auto* t = (Toplevel*)d->UserData;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* w = g.CurrentWindow;

    if (!g.ActiveId || g.ActiveIdWindow != w) {
        return;
    }

    bool resizing = false;

    for (int dir = 0; dir < 4 && !resizing; dir++) {
        resizing = g.ActiveId == ImGui::GetWindowResizeBorderID(w, (ImGuiDir)dir);
    }

    for (int n = 0; n < 4 && !resizing; n++) {
        resizing = g.ActiveId == ImGui::GetWindowResizeCornerID(w, n);
    }

    if (!resizing) {
        return;
    }

    t->dragW = d->DesiredSize.x;
    t->dragH = d->DesiredSize.y;
    d->DesiredSize = ImVec2(t->applyW, t->applyH);
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
        drawSettingsMenu(settings);

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
        ImGui::EndMainMenuBar();
    }

    drawCalendar(width, calendarToggle, &calendarState);
    calendarToggle = false;

    if (notes) {
        drawToasts(*notes, *icons, *this, width, uiScale);
    }

    {
        InspectorInfo info;

        info.frameMs = frameMs;
        info.frameIdx = frameMsIdx;
        info.textures = textures.length();
        info.dmabufCache = dmabufCache.length();
        info.hwCursorKind = hwKind;
        info.hwCursorVisible = hwVisible;
        drawInspector(scene, info, uiScale, inspectorToggle, &inspectorState);
        inspectorToggle = false;
    }

    // client frames are half-width
    const ImVec2 fullPad = ImGui::GetStyle().WindowPadding;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fullPad.x * 0.5f, fullPad.y * 0.5f));

    int i = 0;

    for (Toplevel* t : scene.toplevels) {
        Surface* root = t->surface;

        if (!t->mapped || !root || !root->texture) {
            if (root) {
                markTreeUnhovered(*root);
            }

            continue;
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

        if (!t->docked && !t->fullscreen) {
            // the window is a function of the client's committed geometry:
            // a border drag never resizes it directly (the constraint
            // callback pins it), it only asks — the size steps here, when
            // the geometry answers; initial map sizing and the return from
            // fullscreen are the same rule
            ImGui::SetNextWindowSize(ImVec2(t->applyW, t->applyH), ImGuiCond_Always);
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(FLT_MAX, FLT_MAX), toplevelSizeCb, t);
        }

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        // csd clients (gtk) bring their own header bar; ours would double it
        if (t->csd) {
            flags |= ImGuiWindowFlags_NoTitleBar;
        }

        if (t->fullscreen) {
            // NoDocking alone does not pull a docked window out of its node
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2((float)width, (float)height), ImGuiCond_Always);
            flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;
        }

        bool stayOpen = true;

        if (ImGui::Begin(label.cStr(), t->csd ? nullptr : &stayOpen, flags)) {
            t->docked = ImGui::IsWindowDocked();

            if (ImGui::IsWindowFocused()) {
                scene.focusedToplevel = t;
            }

            if (t->raiseRequested) {
                t->raiseRequested = false;
                ImGui::SetWindowFocus();
            }

            // client-initiated move/resize is dormant until it returns at
            // the dock-group level; the flags are consumed so they do not
            // go stale
            t->moveRequested = false;
            t->resizeEdges = 0;

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
            }

            t->dragW = 0.f;
            t->dragH = 0.f;

            ImVec2 origin = ImGui::GetCursorScreenPos();

            drawSurfaceTree(*root, origin.x, origin.y);

        } else {
            markTreeUnhovered(*root);
        }

        ImGui::End();

        if (!stayOpen) {
            t->closeRequested = true;
        }
    }

    ImGui::PopStyleVar();

    for (Popup* p : scene.popups) {
        Surface* ps = p->surface;

        if (!p->mapped || !ps || !ps->texture || !p->parent) {
            if (ps) {
                markTreeUnhovered(*ps);
            }

            continue;
        }

        drawSurfaceTreeOverlay(*ps, p->parent->imgX + (float)p->parent->geomX() + (float)p->x, p->parent->imgY + (float)p->parent->geomY() + (float)p->y);
    }

    if (scene.dragIcon && scene.dragIcon->texture) {
        ImVec2 mp = ImGui::GetMousePos();

        drawSurfaceTreeOverlay(*scene.dragIcon, mp.x + 4, mp.y + 4);
    }

    bool overClient = false;

    for (Surface* s : scene.surfaces) {
        // decoration surfaces (cursor image, drag icon) ride the pointer:
        // their hover flags go stale the moment they stop being drawn and
        // would pin this true forever; contentless surfaces likewise keep
        // the flag from their last drawn frame
        if (s == scene.cursorSurface || s == scene.dragIcon) {
            continue;
        }

        if (s->hovered && s->hasContent) {
            overClient = true;
        }
    }

    if (altTabActive && !contains(scene.toplevels, altTabSel)) {
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

        if (drawLauncher(width, height, uiScale, *icons, *this, launcherToggle, &launcherState, cmd)) {
            spawnClient(sv(cmd), scene.socketName);
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

        for (Toplevel* t : scene.toplevels) {
            if (!t->mapped || !t->surface || !t->surface->texture) {
                continue;
            }

            float sw = (float)t->surface->geomW(), sh = (float)t->surface->geomH();
            float tw = sh > 0.f ? th * sw / sh : th;

            total += (tw > th * 2.f ? th * 2.f : tw) + pad;
            count++;
        }

        if (count) {
            float lineH = ImGui::GetFontSize();
            float boxH = th + lineH + pad * 3.f;
            float x = ((float)width - total) / 2.f;
            float y0 = ((float)height - boxH) / 2.f;

            dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + total, y0 + boxH), IM_COL32(18, 18, 24, 235), 8.f * uiScale);
            x += pad;

            for (Toplevel* t : scene.toplevels) {
                if (!t->mapped || !t->surface || !t->surface->texture) {
                    continue;
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
            }
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
}

bool RendererImpl::screenshot(StringView path) {
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
    timespec ft0{};

    clock_gettime(CLOCK_MONOTONIC, &ft0);

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
