
#include "wayland.h"
#include "icon_pool.h"
#include "icon_store.h"

#include "input_sink.h"
#include "frame_listener.h"
#include "keyboard.h"
#include "output.h"
#include "scene.h"
#include "session.h"
#include "util.h"

#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#include <ev.h>
#include <linux-dmabuf-v1-server-protocol.h>
#include <cursor-shape-v1-server-protocol.h>
#include <ext-idle-notify-v1-server-protocol.h>
#include <fractional-scale-v1-server-protocol.h>
#include <idle-inhibit-unstable-v1-server-protocol.h>
#include <keyboard-shortcuts-inhibit-unstable-v1-server-protocol.h>
#include <pointer-constraints-unstable-v1-server-protocol.h>
#include <pointer-gestures-unstable-v1-server-protocol.h>
#include <presentation-time-server-protocol.h>
#include <xdg-toplevel-icon-v1-server-protocol.h>
#include <primary-selection-unstable-v1-server-protocol.h>
#include <relative-pointer-unstable-v1-server-protocol.h>
#include <single-pixel-buffer-v1-server-protocol.h>
#include <xdg-activation-v1-server-protocol.h>
#include <xdg-output-unstable-v1-server-protocol.h>
#include <linux-drm-syncobj-v1-server-protocol.h>
#include <linux/input-event-codes.h>
#include <viewporter-server-protocol.h>
#include <wayland-server-protocol.h>
#include <xdg-decoration-unstable-v1-server-protocol.h>
#include <xdg-shell-server-protocol.h>
#include <xf86drm.h>
#include <xkbcommon/xkbcommon.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

using namespace stl;

namespace {
    struct PopupImpl;
    struct SurfaceImpl;
    struct ToplevelImpl;
    struct WaylandImpl;
    struct ConstraintBox;

    struct TimelineBox {
        WaylandImpl* srv = nullptr;
        u32 handle = 0;
        int refs = 0;
        bool resAlive = true;
    };

    struct XdgSurface {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        SurfaceImpl* surface = nullptr;
        ToplevelImpl* toplevel = nullptr;
        PopupImpl* popup = nullptr;
        bool initialConfigureSent = false;
        bool acked = false;
        RectI pendGeom;
        bool pendGeomSet = false;
    };

    struct SurfaceImpl: public Surface {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;

        struct {
            wl_resource* buffer = nullptr;
            bool newlyAttached = false;
            wl_listener bufferDestroy{};
            bool bufferDestroyArmed = false;
            Vector<wl_resource*> frames;
            bool inputRegionSet = false;
            Vector<RectI> inputRegion;
            RectI damage;
            bool damageAll = false;
            int scale = 0;
        } pending;

        Vector<wl_resource*> frameCbs;
        Vector<wl_resource*> presentFeedbacks;

        wl_resource* dmabufRes = nullptr;
        wl_listener dmabufDestroy{};
        bool dmabufDestroyArmed = false;

        wl_resource* vpRes = nullptr;
        double pendSx = -1, pendSy = -1, pendSw = -1, pendSh = -1;
        int pendDw = -1, pendDh = -1;

        wl_resource* fracRes = nullptr;
        ConstraintBox* constraint = nullptr;
        wl_resource* kbInhibitRes = nullptr;

        wl_resource* syncRes = nullptr;
        TimelineBox* pendAcqTl = nullptr;
        TimelineBox* pendRelTl = nullptr;
        TimelineBox* heldAcqTl = nullptr;
        TimelineBox* heldRelTl = nullptr;
        u64 pendAcqPt = 0, pendRelPt = 0, heldRelPt = 0;

        XdgSurface* xdg = nullptr;
    };

    struct SubsurfaceImpl: public Subsurface {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;

        int pendingX = 0, pendingY = 0;
        bool pendingPos = false;
        bool sync = true;

        struct {
            bool valid = false;
            bool hasContent = false;
            int width = 0, height = 0;
            Vector<u8> pixels;
            Vector<wl_resource*> frames;
        } cache;

        bool effectiveSync() const;
    };

    struct ToplevelImpl: public Toplevel {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        XdgSurface* xdg = nullptr;
        int cfgW = 0, cfgH = 0;
        int prevW = 0, prevH = 0;

        // pool icon built from client pixels (xdg-toplevel-icon); wayland
        // owns it: released on replace and on destroy
        Icon* ownIcon = nullptr;
    };

    struct PopupImpl: public Popup {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        XdgSurface* xdg = nullptr;
        int w = 0, h = 0;
    };

    struct RegionBox {
        WaylandImpl* srv = nullptr;
        Vector<RectI> rects;
    };

    // xdg-toplevel-icon: pixels are copied out of the wl_shm buffer right
    // at add_buffer time, the largest reasonable size wins
    struct IconBox {
        WaylandImpl* srv = nullptr;
        char name[128] = "";
        Vector<u32> pixels;
        int w = 0, h = 0;
    };

    struct ConstraintBox {
        WaylandImpl* srv = nullptr;
        SurfaceImpl* surface = nullptr;
        wl_resource* res = nullptr;
        bool isLock = false;
        bool oneshot = false;
        bool dead = false;
        bool hasRegion = false;
        RectI regionBox;
    };

    struct Positioner {
        WaylandImpl* srv = nullptr;

        int w = 0, h = 0;
        int ax = 0, ay = 0, aw = 0, ah = 0;
        u32 anchor = XDG_POSITIONER_ANCHOR_NONE;
        u32 gravity = XDG_POSITIONER_GRAVITY_NONE;
        int dx = 0, dy = 0;

        void place(int& outX, int& outY) const;
    };

    struct BufferBox {
        WaylandImpl* srv = nullptr;
        DmabufBuffer buf;
    };

    struct SpbBox {
        WaylandImpl* srv = nullptr;
        u32 argb = 0;
    };

    struct Params {
        WaylandImpl* srv = nullptr;
        BufferBox* pending = nullptr;
    };

    struct Mime {
        char s[128] = {};
    };

    struct DataSource {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        bool primary = false;
        Vector<Mime> mimes;
        Vector<wl_resource*> offers;
        u32 dndActions = 0;
        bool dropPerformed = false;
    };

    struct SeatState {
        WaylandImpl* srv = nullptr;

        Vector<wl_resource*> keyboards;
        Vector<wl_resource*> pointers;
        Vector<wl_resource*> dataDevices;
        Vector<wl_resource*> primaryDevices;
        Vector<wl_resource*> relPointers;
        Vector<wl_resource*> swipes;
        Vector<wl_resource*> pinches;
        Vector<wl_resource*> holds;

        ConstraintBox* activeConstraint = nullptr;

        DataSource* clipboard = nullptr;
        DataSource* primarySel = nullptr;

        DataSource* dragSource = nullptr;
        Surface* dragTarget = nullptr;
        u32 lastPressedButton = 0;
        Vector<u32> pressedButtons;

        void releaseAllKeys();
        void setSelection(DataSource* src, bool primary);
        void sendSelections(wl_client* client);
        void sourceGone(DataSource* src);
        void startDrag(DataSource* src, Surface* icon);
        void dragMotion();
        void endDrag();

        Keyboard* kb = nullptr;
        bool uiCaptured = false;

        Toplevel* kbFocus = nullptr;
        Surface* kbOverride = nullptr;
        Surface* ptrFocus = nullptr;
        int buttonsDown = 0;

        double curX = 0, curY = 0;
        Vector<u32> pressedKeys;
        u32 modsDepressed = 0, modsLatched = 0, modsLocked = 0, modsGroup = 0;

        SeatState(WaylandImpl& impl);
        ~SeatState() noexcept;

        bool sameClient(wl_resource* res, Toplevel* t);
        bool sameClientS(wl_resource* res, Surface* s);
        Surface* pickInTree(Surface& s);
        Surface* pickPointerTarget();
        void pointerSetFocus(Surface* s, double sx, double sy);

        void handleMotion(double x, double y);
        void handleButton(u32 button, bool pressed);
        void handleScroll(double dx, double dy);
        void handleKey(u32 code, bool pressed);

        void handleRelMotion(double dx, double dy, double dxRaw, double dyRaw);
        void handleSwipeBegin(u32 fingers);
        void handleSwipeUpdate(double dx, double dy);
        void handleSwipeEnd(bool cancelled);
        void handlePinchBegin(u32 fingers);
        void handlePinchUpdate(double dx, double dy, double scale, double rotation);
        void handlePinchEnd(bool cancelled);
        void handleHoldBegin(u32 fingers);
        void handleHoldEnd(bool cancelled);

        void constraintActivate();
        void constraintDeactivate();
        void updateConfineRect();

        wl_resource* kbTargetRes();
        void kbSendLeave(wl_resource* target);
        void kbSendEnter(wl_resource* target);
        void updateModifiers();
        void layoutIndicator();

        void focusToplevel(Toplevel* t);
        void popupGrabStart(Popup* p);
        void popupGone(Popup* p);
        void surfaceGone(Surface* s);
        void toplevelGone(Toplevel* t);
    };

    struct WaylandImpl: public Wayland, public InputSink, public FrameListener, public SessionListener, public IconStoreListener {
        ObjPool* pool = nullptr;
        struct ev_loop* loop = nullptr;
        Scene* scene = nullptr;
        wl_display* display = nullptr;
        wl_event_loop* wlLoop = nullptr;

        const char* socketName = nullptr;
        Keyboard* keyboard = nullptr;
        Vector<DmabufFormat> formats;
        u64 mainDevice = 0;
        int fbTableFd = -1;
        u32 fbTableSize = 0;
        u64 tokenCounter = 0;

        struct WmBasePing {
            wl_resource* res = nullptr;
            bool acked = true;
        };

        Vector<WmBasePing> wmBases;
        ev_timer pingTimer{};
        u32 pingSerial = 0;

        SeatState seat;

        ev_io wlIo{};
        ev_prepare flushPrepare{};
        ev_signal sigInt{}, sigTerm{};
        bool watchersStarted = false;

        u64 nextToplevelId = 1;

        ObjList<SurfaceImpl>* surfaceAlloc = nullptr;
        ObjList<SubsurfaceImpl>* subsurfaceAlloc = nullptr;
        ObjList<XdgSurface>* xdgSurfaceAlloc = nullptr;
        ObjList<ToplevelImpl>* toplevelAlloc = nullptr;
        ObjList<PopupImpl>* popupAlloc = nullptr;
        ObjList<RegionBox>* regionAlloc = nullptr;
        ObjList<Positioner>* positionerAlloc = nullptr;
        ObjList<BufferBox>* dmabufBoxAlloc = nullptr;
        ObjList<DataSource>* dataSourceAlloc = nullptr;
        ObjList<SpbBox>* spbAlloc = nullptr;
        ObjList<Params>* dmabufParamsAlloc = nullptr;
        ObjList<ConstraintBox>* constraintAlloc = nullptr;
        ObjList<IconBox>* iconAlloc = nullptr;
        IconPool* iconPool = nullptr;
        IconStore* icons = nullptr;
        ObjList<TimelineBox>* timelineAlloc = nullptr;
        int drmFd = -1;

        struct IdleNotif {
            WaylandImpl* srv = nullptr;
            wl_resource* res = nullptr;
            bool idled = false;
            ev_timer timer{};
        };

        ObjList<IdleNotif>* idleAlloc = nullptr;
        Vector<IdleNotif*> idleNotifs;
        int idleInhibitors = 0;
        ::Output* output = nullptr;
        double dpmsSec = 0;
        bool dpmsOff = false;
        ev_timer dpmsTimer{};

        void activity();

        WaylandImpl(ObjPool* p, struct ev_loop* evLoop, Scene& scn, const WaylandConfig& cfg);
        ~WaylandImpl() noexcept;

        void run() override;

        // icon store reload: re-resolve every window still on a .desktop
        // match; client-set icons are not ours to touch
        void iconsReloaded() override {
            for (Toplevel* tl : scene->toplevels) {
                if (!tl->iconFromClient) {
                    tl->icon = icons->forAppId(StringView(tl->appId));
                }
            }

            scene->needsFrame = true;
        }

        InputSink* sink() override {
            return this;
        }

        FrameListener* frameListener() override {
            return this;
        }

        SessionListener* sessionListener() override {
            return this;
        }

        void sessionEnabled() override {
        }

        void sessionDisabled() override {
            seat.releaseAllKeys();
        }

        void motion(double x, double y) override {
            activity();
            seat.handleMotion(x, y);
        }

        void button(u32 btn, bool pressed) override {
            activity();
            seat.handleButton(btn, pressed);
        }

        void key(u32 code, bool pressed) override {
            activity();
            seat.handleKey(code, pressed);
        }

        void scroll(double dx, double dy) override {
            activity();
            seat.handleScroll(dx, dy);
        }

        // called by the renderer after every key event: keeps client-visible
        // modifiers fresh and applies the kwin-style release-all on the
        // rising edge of ui keyboard capture
        void modsChanged() override {
            bool cap = scene->kbCaptured;

            if (cap && !seat.uiCaptured) {
                seat.releaseAllKeys();
            }

            seat.uiCaptured = cap;
            seat.updateModifiers();
        }

        void relMotion(double dx, double dy, double dxRaw, double dyRaw) override {
            activity();
            seat.handleRelMotion(dx, dy, dxRaw, dyRaw);
        }

        void swipeBegin(u32 fingers) override {
            activity();
            seat.handleSwipeBegin(fingers);
        }

        void swipeUpdate(double dx, double dy) override {
            seat.handleSwipeUpdate(dx, dy);
        }

        void swipeEnd(bool cancelled) override {
            seat.handleSwipeEnd(cancelled);
        }

        void pinchBegin(u32 fingers) override {
            activity();
            seat.handlePinchBegin(fingers);
        }

        void pinchUpdate(double dx, double dy, double scale, double rotation) override {
            seat.handlePinchUpdate(dx, dy, scale, rotation);
        }

        void pinchEnd(bool cancelled) override {
            seat.handlePinchEnd(cancelled);
        }

        void holdBegin(u32 fingers) override {
            activity();
            seat.handleHoldBegin(fingers);
        }

        void holdEnd(bool cancelled) override {
            seat.handleHoldEnd(cancelled);
        }

        void frameShown(u32 msec) override;

        bool formatSupported(u32 fourcc, u64 modifier) const;
        void createGlobals();
    };

    wl_resource* resOf(Surface* s) {
        return ((SurfaceImpl*)s)->res;
    }

    SubsurfaceImpl& impl(Subsurface* sub) {
        return *(SubsurfaceImpl*)sub;
    }

    void wlIoCb(struct ev_loop*, ev_io* w, int) {
        auto* s = (WaylandImpl*)w->data;

        wl_event_loop_dispatch(s->wlLoop, 0);
    }

    void flushCb(struct ev_loop*, ev_prepare* w, int) {
        auto* s = (WaylandImpl*)w->data;

        wl_display_flush_clients(s->display);
    }

    void signalCb(struct ev_loop* loop, ev_signal*, int) {
        ev_break(loop, EVBREAK_ALL);
    }

    void xdgToplevelConfigureSize(ToplevelImpl& t, int w, int h);
    void xdgToplevelReconfigure(ToplevelImpl& t);

    void wmBasePong(wl_client*, wl_resource* res, u32) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);

        for (size_t i = 0; i < srv->wmBases.length(); i++) {
            if (srv->wmBases[i].res == res) {
                srv->wmBases.mut(i).acked = true;
            }
        }
    }

    void wmBaseResourceDestroyed(wl_resource* res) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);

        for (size_t i = 0; i < srv->wmBases.length(); i++) {
            if (srv->wmBases[i].res == res) {
                srv->wmBases.mut(i) = srv->wmBases.back();
                srv->wmBases.popBack();

                break;
            }
        }
    }

    void pingTimerCb(struct ev_loop*, ev_timer* w, int) {
        auto* srv = (WaylandImpl*)w->data;

        for (size_t i = 0; i < srv->wmBases.length(); i++) {
            auto& p = srv->wmBases.mut(i);

            if (!p.acked) {
                sysE << "imway: client is not answering ping"_sv << endL;
            }

            p.acked = false;
            xdg_wm_base_send_ping(p.res, ++srv->pingSerial);
        }
    }

    void fireFrameCallbacks(SurfaceImpl& s, u32 t) {
        Vector<wl_resource*> cbs;

        cbs.xchg(s.frameCbs);

        for (wl_resource* cb : cbs) {
            wl_callback_send_done(cb, t);
            wl_resource_destroy(cb);
        }

        if (!s.presentFeedbacks.empty()) {
            // prefer the kernel's pageflip timestamp + vblank sequence over
            // "now": mpv & co feed these into their vsync phase estimators
            u64 flipNs = 0;
            u32 seq = 0;
            u32 flags;

            if (s.srv->output && s.srv->output->lastFlip(flipNs, seq)) {
                flags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC | WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK | WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION;
            } else {
                timespec ts{};

                clock_gettime(CLOCK_MONOTONIC, &ts);
                flipNs = (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
                flags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC;
            }

            u64 sec = flipNs / 1000000000ull;
            u32 nsec = (u32)(flipNs % 1000000000ull);
            u32 refreshNs = s.srv->scene->hz > 0 ? (u32)(1e9 / s.srv->scene->hz) : 0;
            Vector<wl_resource*> fbs;

            fbs.xchg(s.presentFeedbacks);

            for (wl_resource* fb : fbs) {
                wl_resource_set_user_data(fb, nullptr);
                wp_presentation_feedback_send_presented(fb, (u32)(sec >> 32), (u32)sec, nsec, refreshNs, 0, seq, flags);
                wl_resource_destroy(fb);
            }
        }

        for (Subsurface* c : s.stackBelow) {
            if (c->surface) {
                fireFrameCallbacks(*(SurfaceImpl*)c->surface, t);
            }
        }

        for (Subsurface* c : s.stackAbove) {
            if (c->surface) {
                fireFrameCallbacks(*(SurfaceImpl*)c->surface, t);
            }
        }
    }

    void copyBounded(char* dst, size_t cap, const char* src) {
        StringView s(src);
        size_t len = s.length() < cap ? s.length() : cap - 1;

        memcpy(dst, s.data(), len);
        dst[len] = 0;
    }

    void resDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void unlinkFromParent(SubsurfaceImpl&);
    void applySubsurfaceCache(SubsurfaceImpl&);
    void xdgHandleCommit(SurfaceImpl&);
    void xdgPopupDismiss(PopupImpl&);
    void viewportApplyPending(SurfaceImpl&);
    void viewportSurfaceGone(SurfaceImpl&);
    void fracSurfaceGone(SurfaceImpl&);
    void constraintSurfaceGone(SurfaceImpl&);
    void kbInhibitSurfaceGone(SurfaceImpl&);
    void syncSurfaceGone(SurfaceImpl&);
    DmabufBuffer* dmabufFromRes(wl_resource*);
    struct SpbBox;
    SpbBox* spbFromRes(wl_resource*);

    SurfaceImpl* surfaceFrom(wl_resource* res) {
        return (SurfaceImpl*)wl_resource_get_user_data(res);
    }

    void detachPendingBuffer(SurfaceImpl& s) {
        if (s.pending.bufferDestroyArmed) {
            wl_list_remove(&s.pending.bufferDestroy.link);
            s.pending.bufferDestroyArmed = false;
        }

        s.pending.buffer = nullptr;
    }

    void pendingBufferDestroyed(wl_listener* l, void*) {
        SurfaceImpl* s = wl_container_of(l, s, pending.bufferDestroy);

        s->pending.buffer = nullptr;
        s->pending.bufferDestroyArmed = false;
        wl_list_remove(&s->pending.bufferDestroy.link);
    }

    void tlUnref(TimelineBox* t) {
        if (!t) {
            return;
        }

        if (--t->refs == 0 && !t->resAlive) {
            drmSyncobjDestroy(t->srv->drmFd, t->handle);
            t->srv->timelineAlloc->release(t);
        }
    }

    // the renderer is synchronous (it waits its own fence every frame), so by
    // the time a buffer is released all GPU reads are done and the release
    // point can be signalled right here
    void syncReleaseHeld(SurfaceImpl& s) {
        if (s.heldRelTl) {
            drmSyncobjTimelineSignal(s.srv->drmFd, &s.heldRelTl->handle, &s.heldRelPt, 1);
        }

        tlUnref(s.heldAcqTl);
        tlUnref(s.heldRelTl);
        s.heldAcqTl = s.heldRelTl = nullptr;
        s.syncAcquireWait = false;
    }

    void heldDmabufDestroyed(wl_listener* l, void*) {
        SurfaceImpl* s = wl_container_of(l, s, dmabufDestroy);

        syncReleaseHeld(*s);
        s->dmabuf = nullptr;
        s->dmabufRes = nullptr;
        s->dmabufDestroyArmed = false;
        wl_list_remove(&s->dmabufDestroy.link);
    }

    void releaseHeldDmabuf(SurfaceImpl& s) {
        if (!s.dmabufRes) {
            return;
        }

        syncReleaseHeld(s);
        wl_buffer_send_release(s.dmabufRes);

        if (s.dmabufDestroyArmed) {
            wl_list_remove(&s.dmabufDestroy.link);
            s.dmabufDestroyArmed = false;
        }

        s.dmabuf = nullptr;
        s.dmabufRes = nullptr;
    }

    void holdDmabuf(SurfaceImpl& s, wl_resource* buffer, DmabufBuffer* buf) {
        releaseHeldDmabuf(s);

        s.dmabuf = buf;
        s.dmabufRes = buffer;
        s.dmabufDestroy.notify = heldDmabufDestroyed;
        wl_resource_add_destroy_listener(buffer, &s.dmabufDestroy);
        s.dmabufDestroyArmed = true;
    }

    void surfaceDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void surfaceAttach(wl_client*, wl_resource* res, wl_resource* buffer, i32, i32) {
        SurfaceImpl& s = *surfaceFrom(res);

        detachPendingBuffer(s);
        s.pending.buffer = buffer;
        s.pending.newlyAttached = true;

        if (buffer) {
            s.pending.bufferDestroy.notify = pendingBufferDestroyed;
            wl_resource_add_destroy_listener(buffer, &s.pending.bufferDestroy);
            s.pending.bufferDestroyArmed = true;
        }
    }

    void surfaceDamage(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        SurfaceImpl& s = *surfaceFrom(res);

        if (s.vp.hasSrc || s.vp.hasDst) {
            s.pending.damageAll = true;

            return;
        }

        i32 sc = s.bufferScale;

        unionRect(s.pending.damage, {x * sc, y * sc, w * sc, h * sc});
    }

    void frameCallbackDestroyed(wl_resource* cb) {
        SurfaceImpl* s = (SurfaceImpl*)wl_resource_get_user_data(cb);

        if (!s) {
            return;
        }

        removeOne(s->pending.frames, cb);
        removeOne(s->frameCbs, cb);
    }

    void surfaceFrame(wl_client* client, wl_resource* res, u32 id) {
        SurfaceImpl& s = *surfaceFrom(res);
        wl_resource* cb = wl_resource_create(client, &wl_callback_interface, 1, id);

        if (!cb) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(cb, nullptr, &s, frameCallbackDestroyed);
        s.pending.frames.pushBack(cb);
    }

    void surfaceSetOpaqueRegion(wl_client*, wl_resource*, wl_resource*) {
    }

    void surfaceSetInputRegion(wl_client*, wl_resource* res, wl_resource* region) {
        SurfaceImpl& s = *surfaceFrom(res);

        if (!region) {
            s.pending.inputRegionSet = false;
            s.pending.inputRegion.clear();

            return;
        }

        auto* box = (RegionBox*)wl_resource_get_user_data(region);

        s.pending.inputRegionSet = true;
        s.pending.inputRegion.clear();
        s.pending.inputRegion.append(box->rects.begin(), box->rects.length());
    }

    void copyShmBufferTo(wl_shm_buffer& shm, int& outW, int& outH, Vector<u8>& out, const RectI* rect) {
        i32 w = wl_shm_buffer_get_width(&shm);
        i32 h = wl_shm_buffer_get_height(&shm);
        i32 stride = wl_shm_buffer_get_stride(&shm);
        u32 fmt = wl_shm_buffer_get_format(&shm);

        if (fmt != WL_SHM_FORMAT_ARGB8888 && fmt != WL_SHM_FORMAT_XRGB8888) {
            sysE << "imway: unsupported shm format "_sv << fmt << endL;
            outW = outH = 0;

            return;
        }

        bool incremental = rect && !rect->empty() && outW == w && outH == h && out.length() == (size_t)w * h * 4;

        outW = w;
        outH = h;

        wl_shm_buffer_begin_access(&shm);

        auto* src = (const u8*)wl_shm_buffer_get_data(&shm);

        if (incremental) {
            for (i32 y = rect->y; y < rect->y + rect->h; y++) {
                memcpy(out.mutData() + ((size_t)y * w + rect->x) * 4, src + (size_t)y * stride + (size_t)rect->x * 4, (size_t)rect->w * 4);
            }
        } else {
            out.clear();
            out.grow((size_t)w * h * 4);

            for (i32 y = 0; y < h; y++) {
                out.append(src + (size_t)y * stride, (size_t)w * 4);
            }
        }

        wl_shm_buffer_end_access(&shm);
    }

    void copyShmBuffer(SurfaceImpl& s, wl_shm_buffer* shm, const RectI* rect) {
        copyShmBufferTo(*shm, s.width, s.height, s.pixels, rect);

        if (s.width > 0) {
            s.dirty = true;
            s.hasContent = true;
        }
    }

    void applyChildrenCaches(SurfaceImpl& s) {
        Vector<Subsurface*>* stacks[] = {&s.stackBelow, &s.stackAbove};

        for (auto* stack : stacks) {
            for (Subsurface* c : *stack) {
                SubsurfaceImpl& sub = impl(c);

                if (sub.pendingPos) {
                    sub.x = sub.pendingX;
                    sub.y = sub.pendingY;
                    sub.pendingPos = false;
                }

                if (sub.sync) {
                    applySubsurfaceCache(sub);
                }
            }
        }
    }

    void applySubsurfaceCache(SubsurfaceImpl& sub) {
        if (sub.cache.valid) {
            SurfaceImpl& s = *(SurfaceImpl*)sub.surface;

            s.hasContent = sub.cache.hasContent;
            s.width = sub.cache.width;
            s.height = sub.cache.height;

            if (sub.cache.hasContent && !sub.cache.pixels.empty()) {
                s.pixels.xchg(sub.cache.pixels);
                s.dirty = true;
                s.damageAll = true;
            }

            for (wl_resource* cb : sub.cache.frames) {
                s.frameCbs.pushBack(cb);
            }

            sub.cache.frames.clear();
            sub.cache.pixels.clear();
            sub.cache.valid = false;
        }

        if (sub.pendingPos) {
            sub.x = sub.pendingX;
            sub.y = sub.pendingY;
            sub.pendingPos = false;
        }

        for (Subsurface* c : sub.surface->stackBelow) {
            if (impl(c).sync) {
                applySubsurfaceCache(impl(c));
            }
        }

        for (Subsurface* c : sub.surface->stackAbove) {
            if (impl(c).sync) {
                applySubsurfaceCache(impl(c));
            }
        }
    }

    bool syncCommitOk(SurfaceImpl& s) {
        bool acq = s.pendAcqTl != nullptr, rel = s.pendRelTl != nullptr;

        if (!s.pending.newlyAttached || !s.pending.buffer) {
            if (acq || rel) {
                wl_resource_post_error(s.syncRes, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_BUFFER, "sync points set but no buffer attached");

                return false;
            }

            return true;
        }

        if (!acq) {
            wl_resource_post_error(s.syncRes, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_ACQUIRE_POINT, "buffer committed without an acquire point");

            return false;
        }

        if (!rel) {
            wl_resource_post_error(s.syncRes, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_RELEASE_POINT, "buffer committed without a release point");

            return false;
        }

        if (!dmabufFromRes(s.pending.buffer)) {
            wl_resource_post_error(s.syncRes, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_UNSUPPORTED_BUFFER, "explicit sync needs a dmabuf buffer");

            return false;
        }

        if (s.pendAcqTl == s.pendRelTl && s.pendAcqPt >= s.pendRelPt) {
            wl_resource_post_error(s.syncRes, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_CONFLICTING_POINTS, "release point is not after the acquire point");

            return false;
        }

        return true;
    }

    void syncApplyPoints(SurfaceImpl& s) {
        if (!s.pendAcqTl || !s.pendRelTl) {
            return;
        }

        // references move from pending to held; released when the buffer is
        s.heldAcqTl = s.pendAcqTl;
        s.heldRelTl = s.pendRelTl;
        s.heldRelPt = s.pendRelPt;
        s.syncAcquireHandle = s.heldAcqTl->handle;
        s.syncAcquirePoint = s.pendAcqPt;
        s.syncAcquireWait = true;
        s.pendAcqTl = s.pendRelTl = nullptr;
    }

    void surfaceCommit(wl_client*, wl_resource* res) {
        SurfaceImpl& s = *surfaceFrom(res);

        s.srv->scene->needsFrame = true;

        SubsurfaceImpl* sub = (SubsurfaceImpl*)s.sub;
        bool toCache = sub && sub->effectiveSync();

        if (s.syncRes && !syncCommitOk(s)) {
            return;
        }

        if (s.pending.newlyAttached) {
            if (!s.pending.buffer) {
                if (toCache) {
                    sub->cache.valid = true;
                    sub->cache.hasContent = false;
                    sub->cache.width = sub->cache.height = 0;
                    sub->cache.pixels.clear();
                } else {
                    s.hasContent = false;
                    s.width = s.height = 0;
                }
            } else if (wl_shm_buffer* shm = wl_shm_buffer_get(s.pending.buffer)) {
                RectI dmg = s.pending.damage;
                bool all = s.pending.damageAll || dmg.empty();

                clipRect(dmg, wl_shm_buffer_get_width(shm), wl_shm_buffer_get_height(shm));

                if (toCache) {
                    copyShmBufferTo(*shm, sub->cache.width, sub->cache.height, sub->cache.pixels, nullptr);
                    sub->cache.hasContent = sub->cache.width > 0;
                    sub->cache.valid = true;
                } else {
                    copyShmBuffer(s, shm, all ? nullptr : &dmg);

                    if (all) {
                        s.damageAll = true;
                    } else {
                        unionRect(s.damage, dmg);
                    }
                }

                wl_buffer_send_release(s.pending.buffer);
                releaseHeldDmabuf(s);
            } else if (SpbBox* spb = spbFromRes(s.pending.buffer)) {
                if (toCache) {
                    sub->cache.width = sub->cache.height = 1;
                    sub->cache.pixels.clear();
                    sub->cache.pixels.append((const u8*)&spb->argb, 4);
                    sub->cache.hasContent = true;
                    sub->cache.valid = true;
                } else {
                    s.width = s.height = 1;
                    s.pixels.clear();
                    s.pixels.append((const u8*)&spb->argb, 4);
                    s.hasContent = true;
                    s.dirty = true;
                    s.damageAll = true;
                }

                wl_buffer_send_release(s.pending.buffer);
                releaseHeldDmabuf(s);
            } else if (DmabufBuffer* db = dmabufFromRes(s.pending.buffer)) {
                holdDmabuf(s, s.pending.buffer, db);
                syncApplyPoints(s);
                s.width = db->width;
                s.height = db->height;
                s.pixels.clear();
                s.hasContent = true;
                s.dirty = true;
            } else {
                sysE << "imway: unknown buffer type"_sv << endL;
            }

            detachPendingBuffer(s);
            s.pending.newlyAttached = false;
        }

        s.pending.damage = {};
        s.pending.damageAll = false;

        if (s.pending.scale > 0 && s.pending.scale != s.bufferScale) {
            s.bufferScale = s.pending.scale;
            s.damageAll = true;
        }

        s.inputRegionSet = s.pending.inputRegionSet;
        s.inputRegion.clear();
        s.inputRegion.append(s.pending.inputRegion.begin(), s.pending.inputRegion.length());

        if (toCache) {
            for (wl_resource* cb : s.pending.frames) {
                sub->cache.frames.pushBack(cb);
            }

            s.pending.frames.clear();

            return;
        }

        for (wl_resource* cb : s.pending.frames) {
            s.frameCbs.pushBack(cb);
        }

        s.pending.frames.clear();

        viewportApplyPending(s);

        applyChildrenCaches(s);

        if (s.xdg) {
            xdgHandleCommit(s);
        }
    }

    void surfaceSetBufferTransform(wl_client*, wl_resource*, i32) {
    }

    void surfaceSetBufferScale(wl_client*, wl_resource* res, i32 scale) {
        if (scale < 1) {
            wl_resource_post_error(res, WL_SURFACE_ERROR_INVALID_SCALE, "buffer_scale must be >= 1");

            return;
        }

        surfaceFrom(res)->pending.scale = scale;
    }

    void surfaceDamageBuffer(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        unionRect(surfaceFrom(res)->pending.damage, {x, y, w, h});
    }

    void surfaceOffset(wl_client*, wl_resource*, i32, i32) {
    }

    const struct wl_surface_interface surfaceImpl = {
        .destroy = surfaceDestroy,
        .attach = surfaceAttach,
        .damage = surfaceDamage,
        .frame = surfaceFrame,
        .set_opaque_region = surfaceSetOpaqueRegion,
        .set_input_region = surfaceSetInputRegion,
        .commit = surfaceCommit,
        .set_buffer_transform = surfaceSetBufferTransform,
        .set_buffer_scale = surfaceSetBufferScale,
        .damage_buffer = surfaceDamageBuffer,
        .offset = surfaceOffset,
    };

    void surfaceResourceDestroyed(wl_resource* res) {
        SurfaceImpl* s = surfaceFrom(res);
        WaylandImpl* srv = s->srv;

        if (srv->scene->dragIcon == s) {
            srv->scene->dragIcon = nullptr;
        }

        if (srv->seat.dragTarget == s) {
            srv->seat.dragTarget = nullptr;
        }

        if (srv->scene->cursorSurface == s) {
            srv->scene->cursorSurface = nullptr;
            srv->scene->cursorShape = CursorKind::unset;
        }

        detachPendingBuffer(*s);

        for (wl_resource* cb : s->pending.frames) {
            wl_resource_set_user_data(cb, nullptr);
        }

        for (wl_resource* cb : s->frameCbs) {
            wl_resource_set_user_data(cb, nullptr);
        }

        for (wl_resource* fb : s->presentFeedbacks) {
            wl_resource_set_user_data(fb, nullptr);
            wp_presentation_feedback_send_discarded(fb);
        }

        if (s->xdg) {
            s->xdg->surface = nullptr;

            if (s->xdg->toplevel) {
                s->xdg->toplevel->surface = nullptr;
            }

            if (s->xdg->popup) {
                s->xdg->popup->surface = nullptr;
            }
        }

        if (s->sub) {
            unlinkFromParent(impl(s->sub));
            s->sub->surface = nullptr;
        }

        for (Subsurface* c : s->stackBelow) {
            c->parent = nullptr;
        }

        for (Subsurface* c : s->stackAbove) {
            c->parent = nullptr;
        }

        for (Popup* p : srv->scene->popups) {
            if (p->parent == s) {
                p->parent = nullptr;

                if (p->mapped) {
                    xdgPopupDismiss(*(PopupImpl*)p);
                }
            }
        }

        srv->seat.surfaceGone(s);

        releaseHeldDmabuf(*s);
        viewportSurfaceGone(*s);
        fracSurfaceGone(*s);
        constraintSurfaceGone(*s);
        kbInhibitSurfaceGone(*s);
        syncSurfaceGone(*s);

        if (s->texture) {
            srv->scene->orphanedTextures.pushBack(s->texture);
            s->texture = nullptr;
        }

        srv->scene->needsFrame = true;
        removeOne(srv->scene->surfaces, (Surface*)s);
        srv->surfaceAlloc->release(s);
    }

    void regionDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void regionAdd(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        ((RegionBox*)wl_resource_get_user_data(res))->rects.pushBack({x, y, w, h});
    }

    void regionSubtract(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        auto& v = ((RegionBox*)wl_resource_get_user_data(res))->rects;
        size_t keep = 0;

        for (size_t i = 0; i < v.length(); i++) {
            const RectI& r = v[i];

            if (!(r.x >= x && r.y >= y && r.x + r.w <= x + w && r.y + r.h <= y + h)) {
                v.mut(keep++) = r;
            }
        }

        while (v.length() > keep) {
            v.popBack();
        }
    }

    const struct wl_region_interface regionImpl = {
        .destroy = regionDestroy,
        .add = regionAdd,
        .subtract = regionSubtract,
    };

    void regionResourceDestroyed(wl_resource* res) {
        auto* box = (RegionBox*)wl_resource_get_user_data(res);

        box->srv->regionAlloc->release(box);
    }

    void compositorCreateSurface(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* sres = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(res), id);

        if (!sres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* s = srv->surfaceAlloc->make();

        s->srv = srv;
        s->res = sres;
        srv->scene->surfaces.pushBack(s);
        wl_resource_set_implementation(sres, &surfaceImpl, s, surfaceResourceDestroyed);
    }

    void compositorCreateRegion(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* rres = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(res), id);

        if (!rres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* box = srv->regionAlloc->make();

        box->srv = srv;
        wl_resource_set_implementation(rres, &regionImpl, box, regionResourceDestroyed);
    }

    const struct wl_compositor_interface compositorImpl = {
        .create_surface = compositorCreateSurface,
        .create_region = compositorCreateRegion,
    };

    void compositorBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wl_compositor_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &compositorImpl, data, nullptr);
    }

    SubsurfaceImpl* subFrom(wl_resource* res) {
        return (SubsurfaceImpl*)wl_resource_get_user_data(res);
    }

    void unlinkFromParent(SubsurfaceImpl& sub) {
        if (!sub.parent) {
            return;
        }

        removeOne(sub.parent->stackBelow, (Subsurface*)&sub);
        removeOne(sub.parent->stackAbove, (Subsurface*)&sub);
        sub.parent = nullptr;
    }

    void subsurfaceDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void subsurfaceSetPosition(wl_client*, wl_resource* res, i32 x, i32 y) {
        SubsurfaceImpl* sub = subFrom(res);

        if (!sub) {
            return;
        }

        sub->pendingX = x;
        sub->pendingY = y;
        sub->pendingPos = true;
    }

    void subsurfaceRestack(SubsurfaceImpl& sub, Surface* refSurface, bool above) {
        Surface* parent = sub.parent;

        if (!parent) {
            return;
        }

        removeOne(parent->stackBelow, (Subsurface*)&sub);
        removeOne(parent->stackAbove, (Subsurface*)&sub);

        if (refSurface == parent) {
            if (above) {
                insertAt(parent->stackAbove, 0, (Subsurface*)&sub);
            } else {
                parent->stackBelow.pushBack(&sub);
            }

            return;
        }

        Subsurface* ref = refSurface->sub;
        Vector<Subsurface*>* stacks[] = {&parent->stackBelow, &parent->stackAbove};

        for (auto* stack : stacks) {
            long idx = indexOf(*stack, ref);

            if (idx >= 0) {
                insertAt(*stack, above ? (size_t)idx + 1 : (size_t)idx, (Subsurface*)&sub);

                return;
            }
        }

        parent->stackAbove.pushBack(&sub);
    }

    void subsurfacePlaceAbove(wl_client*, wl_resource* res, wl_resource* sibling) {
        SubsurfaceImpl* sub = subFrom(res);

        if (sub && sibling) {
            subsurfaceRestack(*sub, surfaceFrom(sibling), true);
        }
    }

    void subsurfacePlaceBelow(wl_client*, wl_resource* res, wl_resource* sibling) {
        SubsurfaceImpl* sub = subFrom(res);

        if (sub && sibling) {
            subsurfaceRestack(*sub, surfaceFrom(sibling), false);
        }
    }

    void subsurfaceSetSync(wl_client*, wl_resource* res) {
        if (SubsurfaceImpl* sub = subFrom(res)) {
            sub->sync = true;
        }
    }

    void subsurfaceSetDesync(wl_client*, wl_resource* res) {
        SubsurfaceImpl* sub = subFrom(res);

        if (!sub) {
            return;
        }

        sub->sync = false;

        if (!sub->effectiveSync() && sub->cache.valid) {
            applySubsurfaceCache(*sub);
        }
    }

    const struct wl_subsurface_interface subsurfaceImpl = {
        .destroy = subsurfaceDestroy,
        .set_position = subsurfaceSetPosition,
        .place_above = subsurfacePlaceAbove,
        .place_below = subsurfacePlaceBelow,
        .set_sync = subsurfaceSetSync,
        .set_desync = subsurfaceSetDesync,
    };

    void subsurfaceResourceDestroyed(wl_resource* res) {
        SubsurfaceImpl* sub = subFrom(res);

        if (!sub) {
            return;
        }

        unlinkFromParent(*sub);

        for (wl_resource* cb : sub->cache.frames) {
            wl_resource_destroy(cb);
        }

        if (sub->surface) {
            sub->surface->sub = nullptr;
        }

        sub->srv->subsurfaceAlloc->release(sub);
    }

    void subcompositorDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void subcompositorGetSubsurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes, wl_resource* parentRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        SurfaceImpl* surface = surfaceFrom(surfaceRes);
        SurfaceImpl* parent = surfaceFrom(parentRes);

        if (surface->xdg || surface->sub) {
            wl_resource_post_error(res, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "surface already has a role");

            return;
        }

        wl_resource* sres = wl_resource_create(client, &wl_subsurface_interface, wl_resource_get_version(res), id);

        if (!sres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* sub = srv->subsurfaceAlloc->make();

        sub->srv = srv;
        sub->surface = surface;
        sub->parent = parent;
        sub->res = sres;
        surface->sub = sub;
        parent->stackAbove.pushBack(sub);
        wl_resource_set_implementation(sres, &subsurfaceImpl, sub, subsurfaceResourceDestroyed);
    }

    const struct wl_subcompositor_interface subcompositorImpl = {
        .destroy = subcompositorDestroy,
        .get_subsurface = subcompositorGetSubsurface,
    };

    void subcompositorBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wl_subcompositor_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &subcompositorImpl, data, nullptr);
    }

    void toplevelDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void toplevelSetParent(wl_client*, wl_resource*, wl_resource*) {
    }

    void toplevelSetTitle(wl_client*, wl_resource* res, const char* title) {
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(res);

        copyBounded(t->title, sizeof(t->title), title);
    }

    void toplevelSetAppId(wl_client*, wl_resource* res, const char* appId) {
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(res);

        copyBounded(t->appId, sizeof(t->appId), appId);

        // resolve right here: a client icon set via xdg-toplevel-icon wins
        if (!t->iconFromClient && t->srv->icons) {
            t->icon = t->srv->icons->forAppId(StringView(t->appId));
            t->srv->scene->needsFrame = true;
        }
    }

    void toplevelShowWindowMenu(wl_client*, wl_resource*, wl_resource*, u32, i32, i32) {
    }

    void toplevelMove(wl_client*, wl_resource* res, wl_resource*, u32) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (ti && ti->srv->seat.buttonsDown > 0) {
            ti->moveRequested = true;
            ti->srv->scene->needsFrame = true;
        }
    }

    void toplevelResize(wl_client*, wl_resource* res, wl_resource*, u32, u32 edges) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (ti && ti->srv->seat.buttonsDown > 0 && edges != 0) {
            ti->resizeEdges = edges;
            ti->srv->scene->needsFrame = true;
        }
    }

    void toplevelSetMaxSize(wl_client*, wl_resource*, i32, i32) {
    }

    void toplevelSetMinSize(wl_client*, wl_resource*, i32, i32) {
    }

    void toplevelSetMaximized(wl_client*, wl_resource*) {
    }

    void toplevelUnsetMaximized(wl_client*, wl_resource*) {
    }

    void toplevelSetFullscreen(wl_client*, wl_resource* res, wl_resource*) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (!ti || ti->fullscreen) {
            return;
        }

        ti->fullscreen = true;
        ti->prevW = ti->cfgW;
        ti->prevH = ti->cfgH;
        xdgToplevelConfigureSize(*ti, ti->srv->scene->outW, ti->srv->scene->outH);
        ti->srv->scene->needsFrame = true;
    }

    void toplevelUnsetFullscreen(wl_client*, wl_resource* res) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (!ti || !ti->fullscreen) {
            return;
        }

        ti->fullscreen = false;
        ti->winSizeSet = false;
        xdgToplevelConfigureSize(*ti, ti->prevW, ti->prevH);
        ti->srv->scene->needsFrame = true;
    }

    void toplevelSetMinimized(wl_client*, wl_resource*) {
    }

    const struct xdg_toplevel_interface toplevelImpl = {
        .destroy = toplevelDestroy,
        .set_parent = toplevelSetParent,
        .set_title = toplevelSetTitle,
        .set_app_id = toplevelSetAppId,
        .show_window_menu = toplevelShowWindowMenu,
        .move = toplevelMove,
        .resize = toplevelResize,
        .set_max_size = toplevelSetMaxSize,
        .set_min_size = toplevelSetMinSize,
        .set_maximized = toplevelSetMaximized,
        .unset_maximized = toplevelUnsetMaximized,
        .set_fullscreen = toplevelSetFullscreen,
        .unset_fullscreen = toplevelUnsetFullscreen,
        .set_minimized = toplevelSetMinimized,
    };

    void toplevelResourceDestroyed(wl_resource* res) {
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(res);
        WaylandImpl* srv = t->srv;

        srv->seat.toplevelGone(t);

        if (t->xdg) {
            t->xdg->toplevel = nullptr;
        }

        if (t->surface) {
            t->surface->toplevel = nullptr;
        }

        if (t->ownIcon && srv->iconPool) {
            srv->iconPool->release(t->ownIcon);
            t->ownIcon = nullptr;
        }

        removeOne(srv->scene->toplevels, (Toplevel*)t);
        sysO << "imway: toplevel "_sv << (const char*)t->title << " destroyed"_sv << endL;
        srv->scene->needsFrame = true;
        srv->toplevelAlloc->release(t);
    }

    void xdgSurfaceDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void sendConfigure(XdgSurface& xs) {
        if (xs.toplevel) {
            wl_array states;

            wl_array_init(&states);
            xdg_toplevel_send_configure(xs.toplevel->res, 0, 0, &states);
            wl_array_release(&states);
        } else if (xs.popup) {
            PopupImpl& p = *xs.popup;

            xdg_popup_send_configure(p.res, p.x, p.y, p.w, p.h);
        }

        xdg_surface_send_configure(xs.res, wl_display_next_serial(xs.srv->display));
        xs.initialConfigureSent = true;
    }

    void xdgSurfaceGetToplevel(wl_client* client, wl_resource* res, u32 id) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);
        wl_resource* tres = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(res), id);

        if (!tres) {
            wl_client_post_no_memory(client);

            return;
        }

        WaylandImpl* srv = xs->srv;
        auto* t = srv->toplevelAlloc->make();

        t->srv = srv;
        t->res = tres;
        t->xdg = xs;
        t->surface = xs->surface;
        t->id = srv->nextToplevelId++;
        xs->toplevel = t;

        if (xs->surface) {
            xs->surface->toplevel = t;
        }

        srv->scene->toplevels.pushBack(t);
        wl_resource_set_implementation(tres, &toplevelImpl, t, toplevelResourceDestroyed);
    }

    void xdgSurfaceGetPopup(wl_client* client, wl_resource* res, u32 id, wl_resource* parentRes, wl_resource* positionerRes);

    void xdgSurfaceSetWindowGeometry(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);

        xs->pendGeom = {x, y, w, h};
        xs->pendGeomSet = true;
    }

    void xdgSurfaceAckConfigure(wl_client*, wl_resource* res, u32) {
        ((XdgSurface*)wl_resource_get_user_data(res))->acked = true;
    }

    const struct xdg_surface_interface xdgSurfaceImpl = {
        .destroy = xdgSurfaceDestroy,
        .get_toplevel = xdgSurfaceGetToplevel,
        .get_popup = xdgSurfaceGetPopup,
        .set_window_geometry = xdgSurfaceSetWindowGeometry,
        .ack_configure = xdgSurfaceAckConfigure,
    };

    void xdgSurfaceResourceDestroyed(wl_resource* res) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);

        if (xs->surface) {
            xs->surface->xdg = nullptr;
            xs->surface->toplevel = nullptr;
        }

        if (xs->toplevel) {
            xs->toplevel->xdg = nullptr;
            xs->toplevel->surface = nullptr;
        }

        if (xs->popup) {
            xs->popup->xdg = nullptr;
            xs->popup->surface = nullptr;
        }

        xs->srv->xdgSurfaceAlloc->release(xs);
    }

    Positioner* positionerFrom(wl_resource* res) {
        return (Positioner*)wl_resource_get_user_data(res);
    }

    void positionerDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void positionerSetSize(wl_client*, wl_resource* res, i32 w, i32 h) {
        Positioner* p = positionerFrom(res);

        p->w = w;
        p->h = h;
    }

    void positionerSetAnchorRect(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        Positioner* p = positionerFrom(res);

        p->ax = x;
        p->ay = y;
        p->aw = w;
        p->ah = h;
    }

    void positionerSetAnchor(wl_client*, wl_resource* res, u32 a) {
        positionerFrom(res)->anchor = a;
    }

    void positionerSetGravity(wl_client*, wl_resource* res, u32 g) {
        positionerFrom(res)->gravity = g;
    }

    void positionerSetConstraintAdjustment(wl_client*, wl_resource*, u32) {
    }

    void positionerSetOffset(wl_client*, wl_resource* res, i32 x, i32 y) {
        Positioner* p = positionerFrom(res);

        p->dx = x;
        p->dy = y;
    }

    void positionerSetReactive(wl_client*, wl_resource*) {
    }

    void positionerSetParentSize(wl_client*, wl_resource*, i32, i32) {
    }

    void positionerSetParentConfigure(wl_client*, wl_resource*, u32) {
    }

    const struct xdg_positioner_interface positionerImpl = {
        .destroy = positionerDestroy,
        .set_size = positionerSetSize,
        .set_anchor_rect = positionerSetAnchorRect,
        .set_anchor = positionerSetAnchor,
        .set_gravity = positionerSetGravity,
        .set_constraint_adjustment = positionerSetConstraintAdjustment,
        .set_offset = positionerSetOffset,
        .set_reactive = positionerSetReactive,
        .set_parent_size = positionerSetParentSize,
        .set_parent_configure = positionerSetParentConfigure,
    };

    void positionerResourceDestroyed(wl_resource* res) {
        Positioner* p = positionerFrom(res);

        p->srv->positionerAlloc->release(p);
    }

    void popupDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void popupGrab(wl_client*, wl_resource* res, wl_resource*, u32) {
        auto* p = (PopupImpl*)wl_resource_get_user_data(res);

        p->grab = true;
    }

    void popupReposition(wl_client*, wl_resource* res, wl_resource* positioner, u32 token) {
        auto* p = (PopupImpl*)wl_resource_get_user_data(res);
        Positioner* pos = positionerFrom(positioner);

        pos->place(p->x, p->y);
        p->w = pos->w;
        p->h = pos->h;
        xdg_popup_send_repositioned(res, token);
        xdg_popup_send_configure(res, p->x, p->y, p->w, p->h);
        xdg_surface_send_configure(p->xdg->res, wl_display_next_serial(p->srv->display));
        p->srv->scene->needsFrame = true;
    }

    const struct xdg_popup_interface popupImpl = {
        .destroy = popupDestroy,
        .grab = popupGrab,
        .reposition = popupReposition,
    };

    void popupResourceDestroyed(wl_resource* res) {
        auto* p = (PopupImpl*)wl_resource_get_user_data(res);
        WaylandImpl* srv = p->srv;

        srv->seat.popupGone(p);

        if (p->xdg) {
            p->xdg->popup = nullptr;
        }

        removeOne(srv->scene->popups, (Popup*)p);
        srv->scene->needsFrame = true;
        srv->popupAlloc->release(p);
    }

    void xdgSurfaceGetPopup(wl_client* client, wl_resource* res, u32 id, wl_resource* parentRes, wl_resource* positionerRes) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);

        if (!parentRes) {
            wl_resource_post_error(res, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT, "popup without a parent is not supported");

            return;
        }

        auto* parentXs = (XdgSurface*)wl_resource_get_user_data(parentRes);
        wl_resource* pres = wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        WaylandImpl* srv = xs->srv;
        auto* p = srv->popupAlloc->make();

        p->srv = srv;
        p->res = pres;
        p->xdg = xs;
        p->surface = xs->surface;
        p->parent = parentXs->surface;

        Positioner* pos = positionerFrom(positionerRes);

        pos->place(p->x, p->y);
        p->w = pos->w;
        p->h = pos->h;
        xs->popup = p;
        srv->scene->popups.pushBack(p);
        wl_resource_set_implementation(pres, &popupImpl, p, popupResourceDestroyed);
    }

    void wmBaseDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void wmBaseCreatePositioner(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* pres = wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        Positioner* p = srv->positionerAlloc->make();

        p->srv = srv;
        wl_resource_set_implementation(pres, &positionerImpl, p, positionerResourceDestroyed);
    }

    void wmBaseGetXdgSurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        auto* surface = surfaceFrom(surfaceRes);
        wl_resource* xres = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(res), id);

        if (!xres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* xs = srv->xdgSurfaceAlloc->make();

        xs->srv = srv;
        xs->res = xres;
        xs->surface = surface;
        surface->xdg = xs;
        wl_resource_set_implementation(xres, &xdgSurfaceImpl, xs, xdgSurfaceResourceDestroyed);
    }

    void wmBasePong(wl_client*, wl_resource* res, u32);

    const struct xdg_wm_base_interface wmBaseImpl = {
        .destroy = wmBaseDestroy,
        .create_positioner = wmBaseCreatePositioner,
        .get_xdg_surface = wmBaseGetXdgSurface,
        .pong = wmBasePong,
    };

    void wmBaseBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &xdg_wm_base_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &wmBaseImpl, data, wmBaseResourceDestroyed);

        auto* srv = (WaylandImpl*)data;

        srv->wmBases.pushBack({res, true});
    }

    void xdgToplevelConfigureSize(ToplevelImpl& t, int w, int h) {
        wl_array states;

        wl_array_init(&states);

        if (t.activated) {
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_ACTIVATED;
        }

        if (t.fullscreen) {
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_FULLSCREEN;
        }

        // csd windows are "tiled": the toolkits (GTK) then drop their drop
        // shadows, invisible resize margins and rounded corners, which we
        // would otherwise have to crop via window geometry; ssd clients must
        // NOT get it — a tiled window has to fill the size exactly, which
        // disables cell-snapped resizing in terminals (foot resize-by-cells)
        if (t.csd && wl_resource_get_version(t.res) >= XDG_TOPLEVEL_STATE_TILED_LEFT_SINCE_VERSION) {
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_TILED_LEFT;
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_TILED_RIGHT;
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_TILED_TOP;
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_TILED_BOTTOM;
        }

        xdg_toplevel_send_configure(t.res, w, h, &states);
        wl_array_release(&states);
        xdg_surface_send_configure(t.xdg->res, wl_display_next_serial(t.srv->display));
        t.cfgW = w;
        t.cfgH = h;
        sysO << "imway: configure "_sv << (const char*)t.title << " -> "_sv << w << "x"_sv << h << endL;
    }

    void xdgToplevelReconfigure(ToplevelImpl& t) {
        xdgToplevelConfigureSize(t, t.cfgW, t.cfgH);
    }

    void xdgHandleCommit(SurfaceImpl& s) {
        XdgSurface* xs = s.xdg;

        if (!xs) {
            return;
        }

        if (xs->pendGeomSet) {
            s.geom = xs->pendGeom;
            s.hasGeom = true;
            xs->pendGeomSet = false;
            s.srv->scene->needsFrame = true;
        }

        if (!xs->initialConfigureSent) {
            if (s.hasContent) {
                sysE << "imway: client attached a buffer before configure (spec violation)"_sv << endL;
            }

            sendConfigure(*xs);

            return;
        }

        if (xs->toplevel && !xs->toplevel->mapped && s.hasContent && xs->acked) {
            xs->toplevel->mapped = true;
            s.srv->scene->needsFrame = true;
            sysO << "imway: toplevel "_sv << (const char*)xs->toplevel->title << " ("_sv << (const char*)xs->toplevel->appId << ") mapped "_sv << s.width << "x"_sv << s.height << endL;

            s.srv->seat.focusToplevel(xs->toplevel);
        }

        if (xs->toplevel && xs->toplevel->mapped && !s.hasContent) {
            xs->toplevel->mapped = false;
            s.srv->scene->needsFrame = true;
            sysO << "imway: toplevel "_sv << (const char*)xs->toplevel->title << " unmapped"_sv << endL;
        }

        if (xs->popup && !xs->popup->mapped && s.hasContent && xs->acked) {
            xs->popup->mapped = true;
            s.srv->scene->needsFrame = true;
            sysO << "imway: popup mapped "_sv << s.width << "x"_sv << s.height << " at ("_sv << xs->popup->x << ","_sv << xs->popup->y << ")"_sv << (xs->popup->grab ? " grab" : "") << endL;

            if (xs->popup->grab) {
                s.srv->seat.popupGrabStart(xs->popup);
            }
        }

        if (xs->popup && xs->popup->mapped && !s.hasContent) {
            xs->popup->mapped = false;
            s.srv->scene->needsFrame = true;
        }
    }

    void xdgPopupDismiss(PopupImpl& p) {
        if (!p.mapped) {
            return;
        }

        p.mapped = false;
        p.srv->scene->needsFrame = true;

        p.srv->seat.popupGone(&p);

        xdg_popup_send_popup_done(p.res);
    }

    void outputRelease(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    const struct wl_output_interface outputImpl = {.release = outputRelease};

    void outputBind(wl_client* client, void* data, u32 version, u32 id) {
        auto* srv = (WaylandImpl*)data;
        wl_resource* res = wl_resource_create(client, &wl_output_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &outputImpl, srv, nullptr);

        wl_output_send_geometry(res, 0, 0, 340, 210, WL_OUTPUT_SUBPIXEL_UNKNOWN, "imway", "headless", WL_OUTPUT_TRANSFORM_NORMAL);
        wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, srv->scene->outW, srv->scene->outH, (i32)(srv->scene->hz * 1000));

        if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
            wl_output_send_scale(res, 1);
        }

        if (version >= WL_OUTPUT_NAME_SINCE_VERSION) {
            wl_output_send_name(res, "HEADLESS-1");
        }

        if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
            wl_output_send_done(res);
        }
    }

    DataSource* sourceFrom(wl_resource* res) {
        return res ? (DataSource*)wl_resource_get_user_data(res) : nullptr;
    }

    void sourceOffer(wl_client*, wl_resource* res, const char* mime) {
        DataSource* src = sourceFrom(res);
        StringView mv(mime);

        if (!src || src->mimes.length() >= 64 || mv.length() >= sizeof(Mime::s)) {
            return;
        }

        Mime m;

        memcpy(m.s, mv.data(), mv.length());
        m.s[mv.length()] = 0;
        src->mimes.pushBack(m);
    }

    void sourceSetActions(wl_client*, wl_resource* res, u32 actions) {
        if (DataSource* src = sourceFrom(res)) {
            src->dndActions = actions;
        }
    }

    const struct wl_data_source_interface dataSourceImpl = {
        .offer = sourceOffer,
        .destroy = resDestroy,
        .set_actions = sourceSetActions,
    };

    void sourceResourceDestroyed(wl_resource* res) {
        DataSource* src = sourceFrom(res);

        if (!src) {
            return;
        }

        for (wl_resource* o : src->offers) {
            wl_resource_set_user_data(o, nullptr);
        }

        src->srv->seat.sourceGone(src);
        src->srv->dataSourceAlloc->release(src);
    }

    u32 chooseDndAction(u32 offered) {
        if (offered & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) {
            return WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
        }

        if (offered & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) {
            return WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
        }

        return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
    }

    void offerAccept(wl_client*, wl_resource* res, u32, const char* mime) {
        DataSource* src = sourceFrom(res);

        if (src && !src->primary) {
            wl_data_source_send_target(src->res, mime);
        }
    }

    void offerReceive(wl_client*, wl_resource* res, const char* mime, i32 fd) {
        DataSource* src = sourceFrom(res);

        if (src) {
            if (src->primary) {
                zwp_primary_selection_source_v1_send_send(src->res, mime, fd);
            } else {
                wl_data_source_send_send(src->res, mime, fd);
            }
        }

        close(fd);
    }

    void offerFinish(wl_client*, wl_resource* res) {
        DataSource* src = sourceFrom(res);

        if (src && !src->primary && src->dropPerformed && wl_resource_get_version(src->res) >= 3) {
            wl_data_source_send_dnd_finished(src->res);
        }
    }

    void offerSetActions(wl_client*, wl_resource* res, u32 actions, u32) {
        DataSource* src = sourceFrom(res);

        if (!src || src->primary) {
            return;
        }

        u32 action = chooseDndAction(actions & src->dndActions);

        if (wl_resource_get_version(res) >= 3) {
            wl_data_offer_send_action(res, action);
        }

        if (wl_resource_get_version(src->res) >= 3) {
            wl_data_source_send_action(src->res, action);
        }
    }

    const struct wl_data_offer_interface dataOfferImpl = {
        .accept = offerAccept,
        .receive = offerReceive,
        .destroy = resDestroy,
        .finish = offerFinish,
        .set_actions = offerSetActions,
    };

    void offerResourceDestroyed(wl_resource* res) {
        if (DataSource* src = sourceFrom(res)) {
            removeOne(src->offers, res);
        }
    }

    const struct zwp_primary_selection_offer_v1_interface primaryOfferImpl = {
        .receive = offerReceive,
        .destroy = resDestroy,
    };

    wl_resource* makeOffer(wl_resource* device, DataSource* src, bool dnd) {
        wl_client* client = wl_resource_get_client(device);
        u32 version = (u32)wl_resource_get_version(device);
        wl_resource* offer;

        if (src->primary) {
            offer = wl_resource_create(client, &zwp_primary_selection_offer_v1_interface, (int)version, 0);

            if (!offer) {
                return nullptr;
            }

            wl_resource_set_implementation(offer, &primaryOfferImpl, src, offerResourceDestroyed);
            src->offers.pushBack(offer);
            zwp_primary_selection_device_v1_send_data_offer(device, offer);

            for (const Mime& m : src->mimes) {
                zwp_primary_selection_offer_v1_send_offer(offer, m.s);
            }

            return offer;
        }

        offer = wl_resource_create(client, &wl_data_offer_interface, (int)version, 0);

        if (!offer) {
            return nullptr;
        }

        wl_resource_set_implementation(offer, &dataOfferImpl, src, offerResourceDestroyed);
        src->offers.pushBack(offer);
        wl_data_device_send_data_offer(device, offer);

        for (const Mime& m : src->mimes) {
            wl_data_offer_send_offer(offer, m.s);
        }

        if (dnd && version >= 3) {
            wl_data_offer_send_source_actions(offer, src->dndActions);
        }

        return offer;
    }

    void deviceStartDrag(wl_client*, wl_resource* res, wl_resource* sourceRes, wl_resource* originRes, wl_resource* iconRes, u32) {
        auto* seat = (SeatState*)wl_resource_get_user_data(res);
        DataSource* src = sourceFrom(sourceRes);

        if (!src || !seat) {
            return;
        }

        Surface* icon = iconRes ? (Surface*)surfaceFrom(iconRes) : nullptr;

        (void)originRes;
        seat->startDrag(src, icon);
    }

    void deviceSetSelection(wl_client*, wl_resource* res, wl_resource* sourceRes, u32) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            seat->setSelection(sourceFrom(sourceRes), false);
        }
    }

    const struct wl_data_device_interface dataDeviceImpl = {
        .start_drag = deviceStartDrag,
        .set_selection = deviceSetSelection,
        .release = resDestroy,
    };

    void dataDeviceResourceDestroyed(wl_resource* res) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            removeOne(seat->dataDevices, res);
        }
    }

    void managerCreateDataSource(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* s = wl_resource_create(client, &wl_data_source_interface, wl_resource_get_version(res), id);

        if (!s) {
            wl_client_post_no_memory(client);

            return;
        }

        DataSource* src = srv->dataSourceAlloc->make();

        src->srv = srv;
        src->res = s;
        src->primary = false;
        src->mimes.clear();
        src->offers.clear();
        src->dndActions = 0;
        src->dropPerformed = false;
        wl_resource_set_implementation(s, &dataSourceImpl, src, sourceResourceDestroyed);
    }

    void managerGetDataDevice(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* d = wl_resource_create(client, &wl_data_device_interface, wl_resource_get_version(res), id);

        if (!d) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(d, &dataDeviceImpl, &srv->seat, dataDeviceResourceDestroyed);
        srv->seat.dataDevices.pushBack(d);
    }

    const struct wl_data_device_manager_interface dataManagerImpl = {
        .create_data_source = managerCreateDataSource,
        .get_data_device = managerGetDataDevice,
    };

    void dataManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wl_data_device_manager_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &dataManagerImpl, data, nullptr);
    }

    void primarySourceOffer(wl_client*, wl_resource* res, const char* mime) {
        sourceOffer(nullptr, res, mime);
    }

    const struct zwp_primary_selection_source_v1_interface primarySourceImpl = {
        .offer = primarySourceOffer,
        .destroy = resDestroy,
    };

    void primaryDeviceSetSelection(wl_client*, wl_resource* res, wl_resource* sourceRes, u32) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            seat->setSelection(sourceFrom(sourceRes), true);
        }
    }

    const struct zwp_primary_selection_device_v1_interface primaryDeviceImpl = {
        .set_selection = primaryDeviceSetSelection,
        .destroy = resDestroy,
    };

    void primaryDeviceResourceDestroyed(wl_resource* res) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            removeOne(seat->primaryDevices, res);
        }
    }

    void primaryManagerCreateSource(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* s = wl_resource_create(client, &zwp_primary_selection_source_v1_interface, wl_resource_get_version(res), id);

        if (!s) {
            wl_client_post_no_memory(client);

            return;
        }

        DataSource* src = srv->dataSourceAlloc->make();

        src->srv = srv;
        src->res = s;
        src->primary = true;
        src->mimes.clear();
        src->offers.clear();
        src->dndActions = 0;
        src->dropPerformed = false;
        wl_resource_set_implementation(s, &primarySourceImpl, src, sourceResourceDestroyed);
    }

    void primaryManagerGetDevice(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* d = wl_resource_create(client, &zwp_primary_selection_device_v1_interface, wl_resource_get_version(res), id);

        if (!d) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(d, &primaryDeviceImpl, &srv->seat, primaryDeviceResourceDestroyed);
        srv->seat.primaryDevices.pushBack(d);
    }

    const struct zwp_primary_selection_device_manager_v1_interface primaryManagerImpl = {
        .create_source = primaryManagerCreateSource,
        .get_device = primaryManagerGetDevice,
        .destroy = resDestroy,
    };

    void primaryManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_primary_selection_device_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &primaryManagerImpl, data, nullptr);
    }

    void decoSetMode(wl_client*, wl_resource* res, u32) {
        zxdg_toplevel_decoration_v1_send_configure(res, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    void decoUnsetMode(wl_client*, wl_resource* res) {
        zxdg_toplevel_decoration_v1_send_configure(res, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    void decoResourceDestroyed(wl_resource* res) {
        // the client dropped the decoration object: back to csd per spec
        if (auto* t = (ToplevelImpl*)wl_resource_get_user_data(res)) {
            t->csd = true;
            t->srv->scene->needsFrame = true;
        }
    }

    const struct zxdg_toplevel_decoration_v1_interface decoImpl = {
        .destroy = resDestroy,
        .set_mode = decoSetMode,
        .unset_mode = decoUnsetMode,
    };

    void decoManagerGetToplevelDecoration(wl_client* client, wl_resource* res, u32 id, wl_resource* toplevelRes) {
        wl_resource* d = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface, wl_resource_get_version(res), id);

        if (!d) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* t = (ToplevelImpl*)wl_resource_get_user_data(toplevelRes);

        // we always answer SERVER_SIDE, so negotiating at all means our bar
        t->csd = false;
        t->srv->scene->needsFrame = true;

        wl_resource_set_implementation(d, &decoImpl, t, decoResourceDestroyed);
        zxdg_toplevel_decoration_v1_send_configure(d, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    const struct zxdg_decoration_manager_v1_interface decoManagerImpl = {
        .destroy = resDestroy,
        .get_toplevel_decoration = decoManagerGetToplevelDecoration,
    };

    void decoManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zxdg_decoration_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &decoManagerImpl, data, nullptr);
    }

    void viewportDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void viewportSetSource(wl_client*, wl_resource* res, wl_fixed_t x, wl_fixed_t y, wl_fixed_t w, wl_fixed_t h) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_NO_SURFACE, "surface is gone");

            return;
        }

        double dx = wl_fixed_to_double(x), dy = wl_fixed_to_double(y);
        double dw = wl_fixed_to_double(w), dh = wl_fixed_to_double(h);

        if (dx == -1 && dy == -1 && dw == -1 && dh == -1) {
            s->pendSw = s->pendSh = -1;

            return;
        }

        if (dx < 0 || dy < 0 || dw <= 0 || dh <= 0) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_BAD_VALUE, "invalid source rect");

            return;
        }

        s->pendSx = dx;
        s->pendSy = dy;
        s->pendSw = dw;
        s->pendSh = dh;
    }

    void viewportSetDestination(wl_client*, wl_resource* res, i32 w, i32 h) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_NO_SURFACE, "surface is gone");

            return;
        }

        if (w == -1 && h == -1) {
            s->pendDw = s->pendDh = -1;

            return;
        }

        if (w <= 0 || h <= 0) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_BAD_VALUE, "invalid destination");

            return;
        }

        s->pendDw = w;
        s->pendDh = h;
    }

    const struct wp_viewport_interface viewportImpl = {
        .destroy = viewportDestroy,
        .set_source = viewportSetSource,
        .set_destination = viewportSetDestination,
    };

    void viewportResourceDestroyed(wl_resource* res) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            return;
        }

        s->vpRes = nullptr;
        s->pendSw = s->pendSh = -1;
        s->pendDw = s->pendDh = -1;
    }

    void viewporterDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void viewporterGetViewport(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->vpRes) {
            wl_resource_post_error(res, WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS, "surface already has a viewport");

            return;
        }

        wl_resource* vres = wl_resource_create(client, &wp_viewport_interface, wl_resource_get_version(res), id);

        if (!vres) {
            wl_client_post_no_memory(client);

            return;
        }

        s->vpRes = vres;
        wl_resource_set_implementation(vres, &viewportImpl, s, viewportResourceDestroyed);
    }

    const struct wp_viewporter_interface viewporterImpl = {
        .destroy = viewporterDestroy,
        .get_viewport = viewporterGetViewport,
    };

    void viewporterBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_viewporter_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &viewporterImpl, data, nullptr);
    }

    void viewportApplyPending(SurfaceImpl& s) {
        s.vp.hasSrc = s.pendSw > 0;

        if (s.vp.hasSrc) {
            s.vp.sx = s.pendSx;
            s.vp.sy = s.pendSy;
            s.vp.sw = s.pendSw;
            s.vp.sh = s.pendSh;
        }

        s.vp.hasDst = s.pendDw > 0;

        if (s.vp.hasDst) {
            s.vp.dw = s.pendDw;
            s.vp.dh = s.pendDh;
        }
    }

    void viewportSurfaceGone(SurfaceImpl& s) {
        if (s.vpRes) {
            wl_resource_set_user_data(s.vpRes, nullptr);
        }
    }

    // ---- xdg-output ----
    void xdgOutputDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    const struct zxdg_output_v1_interface xdgOutputImpl = {.destroy = xdgOutputDestroy};

    void xdgOutputManagerGetXdgOutput(wl_client* client, wl_resource* res, u32 id, wl_resource* outputRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        u32 version = wl_resource_get_version(res);
        wl_resource* xres = wl_resource_create(client, &zxdg_output_v1_interface, version, id);

        if (!xres) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(xres, &xdgOutputImpl, srv, nullptr);

        zxdg_output_v1_send_logical_position(xres, 0, 0);
        zxdg_output_v1_send_logical_size(xres, srv->scene->outW, srv->scene->outH);

        if (version >= ZXDG_OUTPUT_V1_NAME_SINCE_VERSION) {
            zxdg_output_v1_send_name(xres, "HEADLESS-1");
        }

        // since v3 xdg_output.done is deprecated in favor of wl_output.done
        if (version >= 3) {
            if (wl_resource_get_version(outputRes) >= WL_OUTPUT_DONE_SINCE_VERSION) {
                wl_output_send_done(outputRes);
            }
        } else {
            zxdg_output_v1_send_done(xres);
        }
    }

    const struct zxdg_output_manager_v1_interface xdgOutputManagerImpl = {
        .destroy = xdgOutputDestroy,
        .get_xdg_output = xdgOutputManagerGetXdgOutput,
    };

    void xdgOutputManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zxdg_output_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &xdgOutputManagerImpl, data, nullptr);
    }

    // ---- fractional-scale ----
    void fracResourceDestroyed(wl_resource* res) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->fracRes = nullptr;
        }
    }

    void fracDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    const struct wp_fractional_scale_v1_interface fracImpl = {.destroy = fracDestroy};

    void fracManagerGetFractionalScale(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->fracRes) {
            wl_resource_post_error(res, WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS, "surface already has a fractional scale object");

            return;
        }

        wl_resource* fres = wl_resource_create(client, &wp_fractional_scale_v1_interface, wl_resource_get_version(res), id);

        if (!fres) {
            wl_client_post_no_memory(client);

            return;
        }

        s->fracRes = fres;
        wl_resource_set_implementation(fres, &fracImpl, s, fracResourceDestroyed);

        // scale in 1/120ths; fixed 1.0 for now, output scaling is not wired up yet
        wp_fractional_scale_v1_send_preferred_scale(fres, 120);
    }

    const struct wp_fractional_scale_manager_v1_interface fracManagerImpl = {
        .destroy = fracDestroy,
        .get_fractional_scale = fracManagerGetFractionalScale,
    };

    void fracManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_fractional_scale_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &fracManagerImpl, data, nullptr);
    }

    void fracSurfaceGone(SurfaceImpl& s) {
        if (s.fracRes) {
            wl_resource_set_user_data(s.fracRes, nullptr);
        }
    }

    // ---- relative-pointer ----
    void relPointerResourceDestroyed(wl_resource* res) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            removeOne(seat->relPointers, res);
        }
    }

    void relPointerDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    const struct zwp_relative_pointer_v1_interface relPointerImpl = {.destroy = relPointerDestroy};

    void relPointerManagerGetRelativePointer(wl_client* client, wl_resource* res, u32 id, wl_resource* pointerRes) {
        auto* seat = (SeatState*)wl_resource_get_user_data(pointerRes);
        wl_resource* r = wl_resource_create(client, &zwp_relative_pointer_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(r, &relPointerImpl, seat, relPointerResourceDestroyed);
        seat->relPointers.pushBack(r);
    }

    const struct zwp_relative_pointer_manager_v1_interface relPointerManagerImpl = {
        .destroy = relPointerDestroy,
        .get_relative_pointer = relPointerManagerGetRelativePointer,
    };

    void relPointerManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_relative_pointer_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &relPointerManagerImpl, data, nullptr);
    }

    // ---- pointer-gestures ----
    void gestureResourceDestroyed(Vector<wl_resource*> SeatState::* list, wl_resource* res) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            removeOne(seat->*list, res);
        }
    }

    void swipeResourceDestroyed(wl_resource* res) {
        gestureResourceDestroyed(&SeatState::swipes, res);
    }

    void pinchResourceDestroyed(wl_resource* res) {
        gestureResourceDestroyed(&SeatState::pinches, res);
    }

    void holdResourceDestroyed(wl_resource* res) {
        gestureResourceDestroyed(&SeatState::holds, res);
    }

    const struct zwp_pointer_gesture_swipe_v1_interface gestureSwipeImpl = {.destroy = relPointerDestroy};
    const struct zwp_pointer_gesture_pinch_v1_interface gesturePinchImpl = {.destroy = relPointerDestroy};
    const struct zwp_pointer_gesture_hold_v1_interface gestureHoldImpl = {.destroy = relPointerDestroy};

    template <typename Iface>
    void gestureCreate(wl_client* client, wl_resource* res, u32 id, wl_resource* pointerRes, const wl_interface* iface, const Iface* impl, Vector<wl_resource*> SeatState::* list, void (*destroyed)(wl_resource*)) {
        auto* seat = (SeatState*)wl_resource_get_user_data(pointerRes);
        wl_resource* r = wl_resource_create(client, iface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(r, impl, seat, destroyed);
        (seat->*list).pushBack(r);
    }

    void gesturesGetSwipe(wl_client* client, wl_resource* res, u32 id, wl_resource* pointerRes) {
        gestureCreate(client, res, id, pointerRes, &zwp_pointer_gesture_swipe_v1_interface, &gestureSwipeImpl, &SeatState::swipes, swipeResourceDestroyed);
    }

    void gesturesGetPinch(wl_client* client, wl_resource* res, u32 id, wl_resource* pointerRes) {
        gestureCreate(client, res, id, pointerRes, &zwp_pointer_gesture_pinch_v1_interface, &gesturePinchImpl, &SeatState::pinches, pinchResourceDestroyed);
    }

    void gesturesGetHold(wl_client* client, wl_resource* res, u32 id, wl_resource* pointerRes) {
        gestureCreate(client, res, id, pointerRes, &zwp_pointer_gesture_hold_v1_interface, &gestureHoldImpl, &SeatState::holds, holdResourceDestroyed);
    }

    const struct zwp_pointer_gestures_v1_interface pointerGesturesImpl = {
        .get_swipe_gesture = gesturesGetSwipe,
        .get_pinch_gesture = gesturesGetPinch,
        .release = relPointerDestroy,
        .get_hold_gesture = gesturesGetHold,
    };

    void pointerGesturesBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_pointer_gestures_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &pointerGesturesImpl, data, nullptr);
    }

    // ---- pointer-constraints ----
    RectI regionBounds(wl_resource* regionRes) {
        RectI box;

        if (auto* rb = (RegionBox*)wl_resource_get_user_data(regionRes)) {
            for (const RectI& r : rb->rects) {
                unionRect(box, r);
            }
        }

        return box;
    }

    void constraintResourceDestroyed(wl_resource* res) {
        auto* c = (ConstraintBox*)wl_resource_get_user_data(res);

        if (!c) {
            return;
        }

        SeatState& seat = c->srv->seat;

        if (seat.activeConstraint == c) {
            seat.activeConstraint = nullptr;
            c->srv->scene->pointerLocked = false;
            c->srv->scene->pointerConfined = false;
        }

        if (c->surface) {
            c->surface->constraint = nullptr;
        }

        c->srv->constraintAlloc->release(c);
    }

    void constraintSetRegion(wl_client*, wl_resource* res, wl_resource* regionRes) {
        auto* c = (ConstraintBox*)wl_resource_get_user_data(res);

        if (!c) {
            return;
        }

        c->hasRegion = regionRes != nullptr;

        if (c->hasRegion) {
            c->regionBox = regionBounds(regionRes);
        }

        c->srv->seat.updateConfineRect();
    }

    void lockedSetCursorPositionHint(wl_client*, wl_resource*, wl_fixed_t, wl_fixed_t) {
        // accepted but ignored: the cursor simply stays where it was locked
    }

    const struct zwp_locked_pointer_v1_interface lockedPointerImpl = {
        .destroy = relPointerDestroy,
        .set_cursor_position_hint = lockedSetCursorPositionHint,
        .set_region = constraintSetRegion,
    };

    const struct zwp_confined_pointer_v1_interface confinedPointerImpl = {
        .destroy = relPointerDestroy,
        .set_region = constraintSetRegion,
    };

    void constraintCreate(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes, wl_resource* regionRes, u32 lifetime, bool isLock) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->constraint) {
            wl_resource_post_error(res, ZWP_POINTER_CONSTRAINTS_V1_ERROR_ALREADY_CONSTRAINED, "surface is already constrained");

            return;
        }

        const wl_interface* iface = isLock ? &zwp_locked_pointer_v1_interface : &zwp_confined_pointer_v1_interface;
        wl_resource* r = wl_resource_create(client, iface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        ConstraintBox* c = srv->constraintAlloc->make();

        c->srv = srv;
        c->surface = s;
        c->res = r;
        c->isLock = isLock;
        c->oneshot = lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT;
        c->hasRegion = regionRes != nullptr;

        if (c->hasRegion) {
            c->regionBox = regionBounds(regionRes);
        }

        s->constraint = c;

        if (isLock) {
            wl_resource_set_implementation(r, &lockedPointerImpl, c, constraintResourceDestroyed);
        } else {
            wl_resource_set_implementation(r, &confinedPointerImpl, c, constraintResourceDestroyed);
        }

        if (srv->seat.ptrFocus == s) {
            srv->seat.constraintActivate();
        }
    }

    void constraintsLockPointer(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes, wl_resource*, wl_resource* regionRes, u32 lifetime) {
        constraintCreate(client, res, id, surfaceRes, regionRes, lifetime, true);
    }

    void constraintsConfinePointer(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes, wl_resource*, wl_resource* regionRes, u32 lifetime) {
        constraintCreate(client, res, id, surfaceRes, regionRes, lifetime, false);
    }

    const struct zwp_pointer_constraints_v1_interface pointerConstraintsImpl = {
        .destroy = relPointerDestroy,
        .lock_pointer = constraintsLockPointer,
        .confine_pointer = constraintsConfinePointer,
    };

    void pointerConstraintsBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_pointer_constraints_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &pointerConstraintsImpl, data, nullptr);
    }

    void constraintSurfaceGone(SurfaceImpl& s) {
        if (ConstraintBox* c = s.constraint) {
            SeatState& seat = c->srv->seat;

            if (seat.activeConstraint == c) {
                seat.activeConstraint = nullptr;
                c->srv->scene->pointerLocked = false;
                c->srv->scene->pointerConfined = false;
            }

            c->surface = nullptr;
            s.constraint = nullptr;
        }
    }

    // ---- keyboard-shortcuts-inhibit ----
    void kbInhibitorResourceDestroyed(wl_resource* res) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->kbInhibitRes = nullptr;
        }
    }

    const struct zwp_keyboard_shortcuts_inhibitor_v1_interface kbInhibitorImpl = {.destroy = relPointerDestroy};

    void kbInhibitManagerInhibitShortcuts(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes, wl_resource*) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->kbInhibitRes) {
            wl_resource_post_error(res, ZWP_KEYBOARD_SHORTCUTS_INHIBIT_MANAGER_V1_ERROR_ALREADY_INHIBITED, "surface already has a shortcuts inhibitor");

            return;
        }

        wl_resource* r = wl_resource_create(client, &zwp_keyboard_shortcuts_inhibitor_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        s->kbInhibitRes = r;
        wl_resource_set_implementation(r, &kbInhibitorImpl, s, kbInhibitorResourceDestroyed);

        // imway has no compositor shortcuts to suspend, so this is always on
        zwp_keyboard_shortcuts_inhibitor_v1_send_active(r);
    }

    const struct zwp_keyboard_shortcuts_inhibit_manager_v1_interface kbInhibitManagerImpl = {
        .destroy = relPointerDestroy,
        .inhibit_shortcuts = kbInhibitManagerInhibitShortcuts,
    };

    void kbInhibitManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &kbInhibitManagerImpl, data, nullptr);
    }

    void kbInhibitSurfaceGone(SurfaceImpl& s) {
        if (s.kbInhibitRes) {
            wl_resource_set_user_data(s.kbInhibitRes, nullptr);
        }
    }

    // ---- idle-inhibit ----
    void idleInhibitorResourceDestroyed(wl_resource* res) {
        ((WaylandImpl*)wl_resource_get_user_data(res))->idleInhibitors--;
    }

    const struct zwp_idle_inhibitor_v1_interface idleInhibitorImpl = {.destroy = relPointerDestroy};

    void idleInhibitManagerCreateInhibitor(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &zwp_idle_inhibitor_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(r, &idleInhibitorImpl, srv, idleInhibitorResourceDestroyed);
        srv->idleInhibitors++;
    }

    const struct zwp_idle_inhibit_manager_v1_interface idleInhibitManagerImpl = {
        .destroy = relPointerDestroy,
        .create_inhibitor = idleInhibitManagerCreateInhibitor,
    };

    void idleInhibitManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_idle_inhibit_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &idleInhibitManagerImpl, data, nullptr);
    }

    // ---- ext-idle-notify ----
    void idleNotifCb(struct ev_loop* l, ev_timer* w, int) {
        auto* n = (WaylandImpl::IdleNotif*)w->data;

        if (n->srv->idleInhibitors > 0) {
            ev_timer_again(l, w);

            return;
        }

        if (!n->idled) {
            n->idled = true;
            ext_idle_notification_v1_send_idled(n->res);
        }

        ev_timer_stop(l, w);
    }

    void idleNotificationResourceDestroyed(wl_resource* res) {
        auto* n = (WaylandImpl::IdleNotif*)wl_resource_get_user_data(res);

        ev_timer_stop(n->srv->loop, &n->timer);
        removeOne(n->srv->idleNotifs, n);
        n->srv->idleAlloc->release(n);
    }

    const struct ext_idle_notification_v1_interface idleNotificationImpl = {.destroy = relPointerDestroy};

    void idleNotifierGetNotification(wl_client* client, wl_resource* res, u32 id, u32 timeoutMs, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &ext_idle_notification_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        WaylandImpl::IdleNotif* n = srv->idleAlloc->make();

        n->srv = srv;
        n->res = r;

        double t = timeoutMs > 0 ? timeoutMs / 1000.0 : 0.001;

        ev_timer_init(&n->timer, idleNotifCb, t, t);
        n->timer.data = n;
        ev_timer_again(srv->loop, &n->timer);

        srv->idleNotifs.pushBack(n);
        wl_resource_set_implementation(r, &idleNotificationImpl, n, idleNotificationResourceDestroyed);
    }

    const struct ext_idle_notifier_v1_interface idleNotifierImpl = {
        .destroy = relPointerDestroy,
        .get_idle_notification = idleNotifierGetNotification,
    };

    void idleNotifierBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &ext_idle_notifier_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &idleNotifierImpl, data, nullptr);
    }

    // ---- linux-drm-syncobj ----
    void syncTimelineResourceDestroyed(wl_resource* res) {
        auto* t = (TimelineBox*)wl_resource_get_user_data(res);

        t->resAlive = false;

        if (t->refs == 0) {
            drmSyncobjDestroy(t->srv->drmFd, t->handle);
            t->srv->timelineAlloc->release(t);
        }
    }

    const struct wp_linux_drm_syncobj_timeline_v1_interface syncTimelineImpl = {.destroy = relPointerDestroy};

    void syncSurfaceResourceDestroyed(wl_resource* res) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            return;
        }

        s->syncRes = nullptr;
        tlUnref(s->pendAcqTl);
        tlUnref(s->pendRelTl);
        s->pendAcqTl = s->pendRelTl = nullptr;
    }

    void syncSurfaceSetAcquirePoint(wl_client*, wl_resource* res, wl_resource* tlRes, u32 hi, u32 lo) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_SURFACE, "the surface is gone");

            return;
        }

        auto* t = (TimelineBox*)wl_resource_get_user_data(tlRes);

        t->refs++;
        tlUnref(s->pendAcqTl);
        s->pendAcqTl = t;
        s->pendAcqPt = ((u64)hi << 32) | lo;
    }

    void syncSurfaceSetReleasePoint(wl_client*, wl_resource* res, wl_resource* tlRes, u32 hi, u32 lo) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_SURFACE, "the surface is gone");

            return;
        }

        auto* t = (TimelineBox*)wl_resource_get_user_data(tlRes);

        t->refs++;
        tlUnref(s->pendRelTl);
        s->pendRelTl = t;
        s->pendRelPt = ((u64)hi << 32) | lo;
    }

    const struct wp_linux_drm_syncobj_surface_v1_interface syncSurfaceImpl = {
        .destroy = relPointerDestroy,
        .set_acquire_point = syncSurfaceSetAcquirePoint,
        .set_release_point = syncSurfaceSetReleasePoint,
    };

    void syncManagerGetSurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->syncRes) {
            wl_resource_post_error(res, WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_SURFACE_EXISTS, "surface already has a syncobj object");

            return;
        }

        wl_resource* r = wl_resource_create(client, &wp_linux_drm_syncobj_surface_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        s->syncRes = r;
        wl_resource_set_implementation(r, &syncSurfaceImpl, s, syncSurfaceResourceDestroyed);
    }

    void syncManagerImportTimeline(wl_client* client, wl_resource* res, u32 id, int fd) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        u32 handle = 0;

        if (drmSyncobjFDToHandle(srv->drmFd, fd, &handle) != 0) {
            close(fd);
            wl_resource_post_error(res, WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_INVALID_TIMELINE, "could not import the syncobj timeline");

            return;
        }

        close(fd);

        wl_resource* r = wl_resource_create(client, &wp_linux_drm_syncobj_timeline_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            drmSyncobjDestroy(srv->drmFd, handle);
            wl_client_post_no_memory(client);

            return;
        }

        TimelineBox* t = srv->timelineAlloc->make();

        t->srv = srv;
        t->handle = handle;
        wl_resource_set_implementation(r, &syncTimelineImpl, t, syncTimelineResourceDestroyed);
    }

    const struct wp_linux_drm_syncobj_manager_v1_interface syncManagerImpl = {
        .destroy = relPointerDestroy,
        .get_surface = syncManagerGetSurface,
        .import_timeline = syncManagerImportTimeline,
    };

    void syncManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_linux_drm_syncobj_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &syncManagerImpl, data, nullptr);
    }

    void syncSurfaceGone(SurfaceImpl& s) {
        if (s.syncRes) {
            wl_resource_set_user_data(s.syncRes, nullptr);
        }

        tlUnref(s.pendAcqTl);
        tlUnref(s.pendRelTl);
        s.pendAcqTl = s.pendRelTl = nullptr;
    }

    void dpmsTimerCb(struct ev_loop* l, ev_timer* w, int) {
        auto* srv = (WaylandImpl*)w->data;

        if (srv->idleInhibitors > 0) {
            ev_timer_again(l, w);

            return;
        }

        if (!srv->dpmsOff && srv->output) {
            srv->dpmsOff = true;
            srv->output->setPowerSave(false);
        }

        ev_timer_stop(l, w);
    }

    const struct wl_buffer_interface dmabufWlBufferImpl = {.destroy = resDestroy};
    const struct wl_buffer_interface spbWlBufferImpl = {.destroy = resDestroy};

    SpbBox* spbFromRes(wl_resource* res) {
        if (!wl_resource_instance_of(res, &wl_buffer_interface, &spbWlBufferImpl)) {
            return nullptr;
        }

        return (SpbBox*)wl_resource_get_user_data(res);
    }

    void spbBufferResourceDestroyed(wl_resource* res) {
        SpbBox* box = (SpbBox*)wl_resource_get_user_data(res);

        box->srv->spbAlloc->release(box);
    }

    void spbCreateBuffer(wl_client* client, wl_resource* res, u32 id, u32 r, u32 g, u32 b, u32 a) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* buf = wl_resource_create(client, &wl_buffer_interface, 1, id);

        if (!buf) {
            wl_client_post_no_memory(client);

            return;
        }

        SpbBox* box = srv->spbAlloc->make();

        box->srv = srv;
        box->argb = ((a >> 24) << 24) | ((r >> 24) << 16) | ((g >> 24) << 8) | (b >> 24);
        wl_resource_set_implementation(buf, &spbWlBufferImpl, box, spbBufferResourceDestroyed);
    }

    const struct wp_single_pixel_buffer_manager_v1_interface spbManagerImpl = {
        .destroy = resDestroy,
        .create_u32_rgba_buffer = spbCreateBuffer,
    };

    void presentFeedbackDestroyed(wl_resource* res) {
        SurfaceImpl* s = (SurfaceImpl*)wl_resource_get_user_data(res);

        if (s) {
            removeOne(s->presentFeedbacks, res);
        }
    }

    // ---- xdg-toplevel-icon ----
    void iconResourceDestroyed(wl_resource* res) {
        auto* box = (IconBox*)wl_resource_get_user_data(res);

        box->srv->iconAlloc->release(box);
    }

    void iconSetName(wl_client*, wl_resource* res, const char* name) {
        auto* box = (IconBox*)wl_resource_get_user_data(res);

        copyBounded(box->name, sizeof(box->name), name);
    }

    void iconAddBuffer(wl_client*, wl_resource* res, wl_resource* bufferRes, i32) {
        auto* box = (IconBox*)wl_resource_get_user_data(res);
        wl_shm_buffer* shm = wl_shm_buffer_get(bufferRes);

        if (!shm) {
            return; // dmabuf icons: not worth the plumbing
        }

        u32 fmt = wl_shm_buffer_get_format(shm);

        if (fmt != WL_SHM_FORMAT_ARGB8888 && fmt != WL_SHM_FORMAT_XRGB8888) {
            return;
        }

        int w = wl_shm_buffer_get_width(shm);
        int h = wl_shm_buffer_get_height(shm);

        if (w <= 0 || h <= 0 || w > 256 || h > 256 || w <= box->w) {
            return;
        }

        i32 stride = wl_shm_buffer_get_stride(shm);

        wl_shm_buffer_begin_access(shm);

        const u8* src = (const u8*)wl_shm_buffer_get_data(shm);

        box->pixels.clear();

        for (int y = 0; y < h; y++) {
            box->pixels.append((const u32*)(src + (size_t)y * stride), (size_t)w);
        }

        wl_shm_buffer_end_access(shm);
        box->w = w;
        box->h = h;
    }

    const struct xdg_toplevel_icon_v1_interface iconImpl = {
        .destroy = resDestroy,
        .set_name = iconSetName,
        .add_buffer = iconAddBuffer,
    };

    void iconManagerCreateIcon(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &xdg_toplevel_icon_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        IconBox* box = srv->iconAlloc->make();

        box->srv = srv;
        wl_resource_set_implementation(r, &iconImpl, box, iconResourceDestroyed);
    }

    void iconManagerSetIcon(wl_client*, wl_resource* res, wl_resource* toplevelRes, wl_resource* iconRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(toplevelRes);

        if (!t) {
            return;
        }

        if (t->ownIcon && srv->iconPool) {
            srv->iconPool->release(t->ownIcon);
            t->ownIcon = nullptr;
        }

        t->icon = nullptr;
        t->iconFromClient = false;

        if (iconRes) {
            auto* box = (IconBox*)wl_resource_get_user_data(iconRes);

            if (box->pixels.length() && srv->iconPool) {
                Icon* ic = srv->iconPool->acquire();

                ic->width = box->w;
                ic->height = box->h;
                ic->argb.append(box->pixels.data(), box->pixels.length());
                t->ownIcon = ic;
                t->icon = ic;
                t->iconFromClient = true;
            } else if (box->name[0] && srv->icons) {
                t->icon = srv->icons->byName(StringView(box->name));
                t->iconFromClient = t->icon != nullptr;
            }
        }

        if (!t->iconFromClient && srv->icons) {
            // back to the .desktop match
            t->icon = srv->icons->forAppId(StringView(t->appId));
        }

        srv->scene->needsFrame = true;
    }

    const struct xdg_toplevel_icon_manager_v1_interface iconManagerImpl = {
        .destroy = resDestroy,
        .create_icon = iconManagerCreateIcon,
        .set_icon = iconManagerSetIcon,
    };

    void iconManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &xdg_toplevel_icon_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &iconManagerImpl, data, nullptr);
        xdg_toplevel_icon_manager_v1_send_icon_size(res, 48);
        xdg_toplevel_icon_manager_v1_send_done(res);
    }

    void presentationFeedback(wl_client* client, wl_resource* res, wl_resource* surfRes, u32 id) {
        wl_resource* fb = wl_resource_create(client, &wp_presentation_feedback_interface, 1, id);

        if (!fb) {
            wl_client_post_no_memory(client);

            return;
        }

        SurfaceImpl* s = surfaceFrom(surfRes);

        wl_resource_set_implementation(fb, nullptr, s, presentFeedbackDestroyed);
        s->presentFeedbacks.pushBack(fb);
    }

    const struct wp_presentation_interface presentationImpl = {
        .destroy = resDestroy,
        .feedback = presentationFeedback,
    };

    void presentationBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_presentation_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &presentationImpl, data, nullptr);
        wp_presentation_send_clock_id(res, CLOCK_MONOTONIC);
    }

    void activationTokenSetSerial(wl_client*, wl_resource*, u32, wl_resource*) {
    }

    void activationTokenSetAppId(wl_client*, wl_resource*, const char*) {
    }

    void activationTokenSetSurface(wl_client*, wl_resource*, wl_resource*) {
    }


    void activationTokenCommit(wl_client*, wl_resource* res) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        auto& token = sb();

        token << "imway-"_sv << (u64)++srv->tokenCounter;
        xdg_activation_token_v1_send_done(res, token.cStr());
    }

    const struct xdg_activation_token_v1_interface activationTokenImpl = {
        .set_serial = activationTokenSetSerial,
        .set_app_id = activationTokenSetAppId,
        .set_surface = activationTokenSetSurface,
        .commit = activationTokenCommit,
        .destroy = resDestroy,
    };

    void activationGetToken(wl_client* client, wl_resource* res, u32 id) {
        wl_resource* t = wl_resource_create(client, &xdg_activation_token_v1_interface, 1, id);

        if (!t) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(t, &activationTokenImpl, wl_resource_get_user_data(res), nullptr);
    }

    void activationActivate(wl_client*, wl_resource* res, const char* token, wl_resource* surfRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        SurfaceImpl* s = surfaceFrom(surfRes);
        Toplevel* tl = s ? s->rootToplevel() : nullptr;

        if (!tl || !tl->mapped) {
            return;
        }

        sysO << "imway: activation ("_sv << token << ") -> "_sv << (const char*)tl->title << endL;
        srv->seat.focusToplevel(tl);
        tl->raiseRequested = true;
        srv->scene->needsFrame = true;
    }

    const struct xdg_activation_v1_interface activationImpl = {
        .destroy = resDestroy,
        .get_activation_token = activationGetToken,
        .activate = activationActivate,
    };

    void activationBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &xdg_activation_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &activationImpl, data, nullptr);
    }

    void spbManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_single_pixel_buffer_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &spbManagerImpl, data, nullptr);
    }

    DmabufBuffer* dmabufFromRes(wl_resource* res) {
        if (!wl_resource_instance_of(res, &wl_buffer_interface, &dmabufWlBufferImpl)) {
            return nullptr;
        }

        return &((BufferBox*)wl_resource_get_user_data(res))->buf;
    }

    void dmabufBufferResourceDestroyed(wl_resource* res) {
        auto* box = (BufferBox*)wl_resource_get_user_data(res);

        box->srv->scene->deadDmabufs.pushBack(&box->buf);

        for (int i = 0; i < box->buf.nplanes; i++) {
            if (box->buf.fds[i] >= 0) {
                close(box->buf.fds[i]);
            }
        }

        box->srv->dmabufBoxAlloc->release(box);
    }

    Params* paramsFrom(wl_resource* res) {
        return (Params*)wl_resource_get_user_data(res);
    }

    void paramsDestroyResource(wl_resource* res) {
        Params* p = paramsFrom(res);

        if (p->pending) {
            for (int i = 0; i < kDmabufMaxPlanes; i++) {
                if (p->pending->buf.fds[i] >= 0) {
                    close(p->pending->buf.fds[i]);
                }
            }

            p->srv->dmabufBoxAlloc->release(p->pending);
        }

        p->srv->dmabufParamsAlloc->release(p);
    }

    void paramsAdd(wl_client*, wl_resource* res, i32 fd, u32 planeIdx, u32 offset, u32 stride, u32 modifierHi, u32 modifierLo) {
        Params* p = paramsFrom(res);

        if (!p->pending) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED, "params already used");
            close(fd);

            return;
        }

        if (planeIdx >= (u32)kDmabufMaxPlanes) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX, "plane_idx %u out of range", planeIdx);
            close(fd);

            return;
        }

        DmabufBuffer& b = p->pending->buf;

        if (b.fds[planeIdx] >= 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET, "plane %u already set", planeIdx);
            close(fd);

            return;
        }

        b.fds[planeIdx] = fd;
        b.offsets[planeIdx] = offset;
        b.strides[planeIdx] = stride;
        b.modifier = ((u64)modifierHi << 32) | modifierLo;

        if ((int)planeIdx + 1 > b.nplanes) {
            b.nplanes = planeIdx + 1;
        }
    }

    wl_resource* paramsMakeBuffer(wl_client* client, wl_resource* res, u32 bufferId, i32 width, i32 height, u32 format) {
        Params* p = paramsFrom(res);

        if (!p->pending) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED, "params already used");

            return nullptr;
        }

        if (width <= 0 || height <= 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS, "invalid dimensions %dx%d", width, height);

            return nullptr;
        }

        DmabufBuffer& b = p->pending->buf;

        if (b.nplanes == 0 || b.fds[0] < 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "plane 0 missing");

            return nullptr;
        }

        if (!p->srv->formatSupported(format, b.modifier)) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "format 0x%x not supported", format);

            return nullptr;
        }

        wl_resource* bres = wl_resource_create(client, &wl_buffer_interface, 1, bufferId);

        if (!bres) {
            wl_client_post_no_memory(client);

            return nullptr;
        }

        BufferBox* box = p->pending;

        p->pending = nullptr;
        box->buf.width = width;
        box->buf.height = height;
        box->buf.format = format;
        wl_resource_set_implementation(bres, &dmabufWlBufferImpl, box, dmabufBufferResourceDestroyed);

        return bres;
    }

    void paramsCreate(wl_client* client, wl_resource* res, i32 width, i32 height, u32 format, u32) {
        wl_resource* buf = paramsMakeBuffer(client, res, 0, width, height, format);

        if (buf) {
            zwp_linux_buffer_params_v1_send_created(res, buf);
        } else if (!paramsFrom(res)->pending) {
            zwp_linux_buffer_params_v1_send_failed(res);
        }
    }

    void paramsCreateImmed(wl_client* client, wl_resource* res, u32 bufferId, i32 width, i32 height, u32 format, u32) {
        paramsMakeBuffer(client, res, bufferId, width, height, format);
    }

    const struct zwp_linux_buffer_params_v1_interface paramsImpl = {
        .destroy = resDestroy,
        .add = paramsAdd,
        .create = paramsCreate,
        .create_immed = paramsCreateImmed,
    };

    void dmabufCreateParams(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* pres = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* p = srv->dmabufParamsAlloc->make();

        p->srv = srv;
        p->pending = srv->dmabufBoxAlloc->make();
        p->pending->srv = srv;
        wl_resource_set_implementation(pres, &paramsImpl, p, paramsDestroyResource);
    }

    const struct zwp_linux_dmabuf_feedback_v1_interface dmabufFeedbackImpl = {
        .destroy = resDestroy,
    };

    void sendFeedback(WaylandImpl* srv, wl_client* client, wl_resource* parent, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(parent), id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &dmabufFeedbackImpl, srv, nullptr);
        zwp_linux_dmabuf_feedback_v1_send_format_table(res, srv->fbTableFd, srv->fbTableSize);

        wl_array dev;

        wl_array_init(&dev);
        *(u64*)wl_array_add(&dev, sizeof(u64)) = srv->mainDevice;
        zwp_linux_dmabuf_feedback_v1_send_main_device(res, &dev);
        zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(res, &dev);
        wl_array_release(&dev);

        wl_array indices;

        wl_array_init(&indices);

        for (u16 i = 0; i < (u16)srv->formats.length(); i++) {
            *(u16*)wl_array_add(&indices, sizeof(u16)) = i;
        }

        zwp_linux_dmabuf_feedback_v1_send_tranche_formats(res, &indices);
        wl_array_release(&indices);
        zwp_linux_dmabuf_feedback_v1_send_tranche_flags(res, 0);
        zwp_linux_dmabuf_feedback_v1_send_tranche_done(res);
        zwp_linux_dmabuf_feedback_v1_send_done(res);
    }

    void dmabufGetDefaultFeedback(wl_client* client, wl_resource* res, u32 id) {
        sendFeedback((WaylandImpl*)wl_resource_get_user_data(res), client, res, id);
    }

    void dmabufGetSurfaceFeedback(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        sendFeedback((WaylandImpl*)wl_resource_get_user_data(res), client, res, id);
    }

    const struct zwp_linux_dmabuf_v1_interface dmabufImpl = {
        .destroy = resDestroy,
        .create_params = dmabufCreateParams,
        .get_default_feedback = dmabufGetDefaultFeedback,
        .get_surface_feedback = dmabufGetSurfaceFeedback,
    };

    void dmabufBind(wl_client* client, void* data, u32 version, u32 id) {
        auto* srv = (WaylandImpl*)data;
        wl_resource* res = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &dmabufImpl, srv, nullptr);

        for (const DmabufFormat& fm : srv->formats) {
            if (version >= 4) {
                break;
            }

            if (version >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
                zwp_linux_dmabuf_v1_send_modifier(res, fm.fourcc, (u32)(fm.modifier >> 32), (u32)(fm.modifier & 0xffffffff));
            } else {
                zwp_linux_dmabuf_v1_send_format(res, fm.fourcc);
            }
        }
    }

    CursorKind cursorKindFromShape(u32 shape) {
        switch (shape) {
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT:
                return CursorKind::text;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER:
                return CursorKind::hand;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING:
                return CursorKind::grab;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL:
                return CursorKind::move;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE:
                return CursorKind::nsResize;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE:
                return CursorKind::ewResize;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE:
                return CursorKind::neswResize;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE:
                return CursorKind::nwseResize;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP:
                return CursorKind::notAllowed;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT:
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS:
                return CursorKind::wait;
            default:
                return CursorKind::def;
        }
    }

    void cursorShapeDeviceSetShape(wl_client* client, wl_resource* res, u32, u32 shape) {
        auto* seat = (SeatState*)wl_resource_get_user_data(res);

        if (!seat || !seat->ptrFocus) {
            return;
        }

        if (wl_resource_get_client(resOf(seat->ptrFocus)) != client) {
            return;
        }

        Scene* scn = seat->srv->scene;

        scn->cursorSurface = nullptr;
        scn->cursorShape = cursorKindFromShape(shape);
        scn->needsFrame = true;
    }

    const struct wp_cursor_shape_device_v1_interface cursorShapeDeviceImpl = {
        .destroy = resDestroy,
        .set_shape = cursorShapeDeviceSetShape,
    };

    void cursorShapeGetPointer(wl_client* client, wl_resource* res, u32 id, wl_resource* pointerRes) {
        wl_resource* d = wl_resource_create(client, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(res), id);

        if (!d) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(d, &cursorShapeDeviceImpl, wl_resource_get_user_data(pointerRes), nullptr);
    }

    void cursorShapeGetTabletTool(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        wl_resource* d = wl_resource_create(client, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(res), id);

        if (d) {
            wl_resource_set_implementation(d, &cursorShapeDeviceImpl, nullptr, nullptr);
        }
    }

    const struct wp_cursor_shape_manager_v1_interface cursorShapeManagerImpl = {
        .destroy = resDestroy,
        .get_pointer = cursorShapeGetPointer,
        .get_tablet_tool_v2 = cursorShapeGetTabletTool,
    };

    void cursorShapeManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_cursor_shape_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &cursorShapeManagerImpl, data, nullptr);
    }

    constexpr u32 kSeatVersion = 5;

    SeatState* seatOf(wl_resource* res) {
        return (SeatState*)wl_resource_get_user_data(res);
    }

    void pointerSetCursor(wl_client* client, wl_resource* res, u32, wl_resource* surfRes, i32 hotX, i32 hotY) {
        SeatState* seat = seatOf(res);

        if (!seat || !seat->ptrFocus) {
            return;
        }

        if (wl_resource_get_client(resOf(seat->ptrFocus)) != client) {
            return;
        }

        Scene* scn = seat->srv->scene;

        scn->cursorSurface = surfRes ? (Surface*)surfaceFrom(surfRes) : nullptr;
        scn->cursorShape = surfRes ? CursorKind::unset : CursorKind::hidden;
        scn->cursorHotX = hotX;
        scn->cursorHotY = hotY;
        scn->needsFrame = true;
    }

    const struct wl_pointer_interface pointerImpl = {
        .set_cursor = pointerSetCursor,
        .release = resDestroy,
    };

    const struct wl_keyboard_interface keyboardImpl = {.release = resDestroy};
    const struct wl_touch_interface touchImpl = {.release = resDestroy};

    void pointerResourceDestroyed(wl_resource* res) {
        removeOne(seatOf(res)->pointers, res);
    }

    void keyboardResourceDestroyed(wl_resource* res) {
        removeOne(seatOf(res)->keyboards, res);
    }

    void seatGetPointer(wl_client* client, wl_resource* res, u32 id) {
        SeatState* seat = seatOf(res);
        wl_resource* p = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(res), id);

        if (!p) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(p, &pointerImpl, seat, pointerResourceDestroyed);
        seat->pointers.pushBack(p);
    }

    void seatGetKeyboard(wl_client* client, wl_resource* res, u32 id) {
        SeatState* seat = seatOf(res);
        wl_resource* k = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(res), id);

        if (!k) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(k, &keyboardImpl, seat, keyboardResourceDestroyed);
        seat->keyboards.pushBack(k);

        wl_keyboard_send_keymap(k, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, seat->kb->keymapFd(), seat->kb->keymapSize());

        if (wl_resource_get_version(k) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
            wl_keyboard_send_repeat_info(k, 25, 600);
        }

        SeatState& s = *seat;

        if (s.kbFocus && s.kbFocus->surface && wl_resource_get_client(resOf(s.kbFocus->surface)) == client) {
            wl_array keys;

            wl_array_init(&keys);

            for (u32 kc : s.pressedKeys) {
                *(u32*)wl_array_add(&keys, sizeof(u32)) = kc;
            }

            wl_keyboard_send_enter(k, wl_display_next_serial(s.srv->display), resOf(s.kbFocus->surface), &keys);
            wl_array_release(&keys);
            wl_keyboard_send_modifiers(k, wl_display_next_serial(s.srv->display), s.modsDepressed, s.modsLatched, s.modsLocked, s.modsGroup);
        }
    }

    void seatGetTouch(wl_client* client, wl_resource* res, u32 id) {
        wl_resource* t = wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(res), id);

        if (t) {
            wl_resource_set_implementation(t, &touchImpl, nullptr, nullptr);
        }
    }

    const struct wl_seat_interface seatImpl = {
        .get_pointer = seatGetPointer,
        .get_keyboard = seatGetKeyboard,
        .get_touch = seatGetTouch,
        .release = resDestroy,
    };

    void seatBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wl_seat_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &seatImpl, data, nullptr);
        wl_seat_send_capabilities(res, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

        if (version >= WL_SEAT_NAME_SINCE_VERSION) {
            wl_seat_send_name(res, "seat0");
        }
    }}

bool SubsurfaceImpl::effectiveSync() const {
    for (const SubsurfaceImpl* s = this; s; s = s->parent ? (const SubsurfaceImpl*)s->parent->sub : nullptr) {
        if (s->sync) {
            return true;
        }
    }

    return false;
}

void Positioner::place(int& outX, int& outY) const {
    int px = ax, py = ay;

    switch (anchor) {
        case XDG_POSITIONER_ANCHOR_TOP:
            px += aw / 2;
            break;
        case XDG_POSITIONER_ANCHOR_BOTTOM:
            px += aw / 2;
            py += ah;
            break;
        case XDG_POSITIONER_ANCHOR_LEFT:
            py += ah / 2;
            break;
        case XDG_POSITIONER_ANCHOR_RIGHT:
            px += aw;
            py += ah / 2;
            break;
        case XDG_POSITIONER_ANCHOR_TOP_LEFT:
            break;
        case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
            py += ah;
            break;
        case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
            px += aw;
            break;
        case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
            px += aw;
            py += ah;
            break;
        default:
            px += aw / 2;
            py += ah / 2;
            break;
    }

    switch (gravity) {
        case XDG_POSITIONER_GRAVITY_TOP:
            px -= w / 2;
            py -= h;
            break;
        case XDG_POSITIONER_GRAVITY_BOTTOM:
            px -= w / 2;
            break;
        case XDG_POSITIONER_GRAVITY_LEFT:
            px -= w;
            py -= h / 2;
            break;
        case XDG_POSITIONER_GRAVITY_RIGHT:
            py -= h / 2;
            break;
        case XDG_POSITIONER_GRAVITY_TOP_LEFT:
            px -= w;
            py -= h;
            break;
        case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
            px -= w;
            break;
        case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
            py -= h;
            break;
        case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
            break;
        default:
            px -= w / 2;
            py -= h / 2;
            break;
    }

    outX = px + dx;
    outY = py + dy;
}

SeatState::SeatState(WaylandImpl& impl) : srv(&impl) {
    kb = impl.keyboard;
    STD_VERIFY(kb);
    layoutIndicator();
}

void SeatState::layoutIndicator() {
    kb->layoutShort(srv->scene->layout);
    srv->scene->needsFrame = true;
}

SeatState::~SeatState() noexcept {
}

bool SeatState::sameClientS(wl_resource* res, Surface* s) {
    return s && wl_resource_get_client(res) == wl_resource_get_client(resOf(s));
}

bool SeatState::sameClient(wl_resource* res, Toplevel* t) {
    return t && t->surface && wl_resource_get_client(res) == wl_resource_get_client(resOf(t->surface));
}

Surface* SeatState::pickInTree(Surface& s) {
    Surface* found = nullptr;

    for (Subsurface* c : s.stackBelow) {
        if (c->surface && c->surface->hasContent) {
            if (Surface* f = pickInTree(*c->surface)) {
                found = f;
            }
        }
    }

    if (s.hovered && s.inputContains(curX - s.imgX, curY - s.imgY)) {
        found = &s;
    }

    for (Subsurface* c : s.stackAbove) {
        if (c->surface && c->surface->hasContent) {
            if (Surface* f = pickInTree(*c->surface)) {
                found = f;
            }
        }
    }

    return found;
}

Surface* SeatState::pickPointerTarget() {
    for (size_t i = srv->scene->popups.length(); i > 0; i--) {
        Popup* p = srv->scene->popups[i - 1];

        if (!p->mapped || !p->surface) {
            continue;
        }

        if (Surface* s = pickInTree(*p->surface)) {
            return s;
        }
    }

    for (Toplevel* t : srv->scene->toplevels) {
        if (!t->mapped || !t->surface) {
            continue;
        }

        if (Surface* s = pickInTree(*t->surface)) {
            return s;
        }
    }

    return nullptr;
}

void SeatState::pointerSetFocus(Surface* s, double sx, double sy) {
    if (ptrFocus == s) {
        return;
    }

    constraintDeactivate();

    if (ptrFocus) {
        u32 serial = wl_display_next_serial(srv->display);

        for (wl_resource* p : pointers) {
            if (sameClientS(p, ptrFocus)) {
                wl_pointer_send_leave(p, serial, resOf(ptrFocus));

                if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                    wl_pointer_send_frame(p);
                }
            }
        }
    }

    ptrFocus = s;
    srv->scene->cursorShape = CursorKind::unset;
    srv->scene->cursorSurface = nullptr;

    if (s) {
        u32 serial = wl_display_next_serial(srv->display);

        for (wl_resource* p : pointers) {
            if (sameClientS(p, s)) {
                wl_pointer_send_enter(p, serial, resOf(s), wl_fixed_from_double(sx), wl_fixed_from_double(sy));

                if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                    wl_pointer_send_frame(p);
                }
            }
        }
    }

    constraintActivate();
}

void SeatState::constraintActivate() {
    activeConstraint = nullptr;

    if (!ptrFocus) {
        return;
    }

    ConstraintBox* c = ((SurfaceImpl*)ptrFocus)->constraint;

    if (!c || c->dead) {
        return;
    }

    activeConstraint = c;

    if (c->isLock) {
        srv->scene->pointerLocked = true;
        zwp_locked_pointer_v1_send_locked(c->res);
    } else {
        srv->scene->pointerConfined = true;
        updateConfineRect();
        zwp_confined_pointer_v1_send_confined(c->res);
    }
}

void SeatState::constraintDeactivate() {
    ConstraintBox* c = activeConstraint;

    if (!c) {
        return;
    }

    activeConstraint = nullptr;
    srv->scene->pointerLocked = false;
    srv->scene->pointerConfined = false;

    if (c->isLock) {
        zwp_locked_pointer_v1_send_unlocked(c->res);
    } else {
        zwp_confined_pointer_v1_send_unconfined(c->res);
    }

    if (c->oneshot) {
        c->dead = true;
    }
}

void SeatState::updateConfineRect() {
    ConstraintBox* c = activeConstraint;

    if (!c || c->isLock || !ptrFocus) {
        return;
    }

    Scene* scn = srv->scene;
    double x0 = ptrFocus->imgX + ptrFocus->geomX(), y0 = ptrFocus->imgY + ptrFocus->geomY();
    double x1 = x0 + ptrFocus->geomW() - 1, y1 = y0 + ptrFocus->geomH() - 1;

    if (c->hasRegion && !c->regionBox.empty()) {
        double rx0 = ptrFocus->imgX + c->regionBox.x;
        double ry0 = ptrFocus->imgY + c->regionBox.y;
        double rx1 = rx0 + c->regionBox.w - 1;
        double ry1 = ry0 + c->regionBox.h - 1;

        x0 = rx0 > x0 ? rx0 : x0;
        y0 = ry0 > y0 ? ry0 : y0;
        x1 = rx1 < x1 ? rx1 : x1;
        y1 = ry1 < y1 ? ry1 : y1;
    }

    scn->confineX0 = x0;
    scn->confineY0 = y0;
    scn->confineX1 = x1;
    scn->confineY1 = y1;
}

void SeatState::handleRelMotion(double dx, double dy, double dxRaw, double dyRaw) {
    if (!ptrFocus || relPointers.empty()) {
        return;
    }

    u64 ut = (u64)nowMsec() * 1000;
    bool sent = false;

    for (wl_resource* r : relPointers) {
        if (sameClientS(r, ptrFocus)) {
            zwp_relative_pointer_v1_send_relative_motion(r, (u32)(ut >> 32), (u32)ut, wl_fixed_from_double(dx), wl_fixed_from_double(dy), wl_fixed_from_double(dxRaw), wl_fixed_from_double(dyRaw));
            sent = true;
        }
    }

    if (!sent) {
        return;
    }

    for (wl_resource* p : pointers) {
        if (sameClientS(p, ptrFocus) && wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
            wl_pointer_send_frame(p);
        }
    }
}

void SeatState::handleSwipeBegin(u32 fingers) {
    if (!ptrFocus) {
        return;
    }

    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : swipes) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_swipe_v1_send_begin(r, serial, t, resOf(ptrFocus), fingers);
        }
    }
}

void SeatState::handleSwipeUpdate(double dx, double dy) {
    if (!ptrFocus) {
        return;
    }

    u32 t = nowMsec();

    for (wl_resource* r : swipes) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_swipe_v1_send_update(r, t, wl_fixed_from_double(dx), wl_fixed_from_double(dy));
        }
    }
}

void SeatState::handleSwipeEnd(bool cancelled) {
    if (!ptrFocus) {
        return;
    }

    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : swipes) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_swipe_v1_send_end(r, serial, t, cancelled);
        }
    }
}

void SeatState::handlePinchBegin(u32 fingers) {
    if (!ptrFocus) {
        return;
    }

    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : pinches) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_pinch_v1_send_begin(r, serial, t, resOf(ptrFocus), fingers);
        }
    }
}

void SeatState::handlePinchUpdate(double dx, double dy, double scale, double rotation) {
    if (!ptrFocus) {
        return;
    }

    u32 t = nowMsec();

    for (wl_resource* r : pinches) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_pinch_v1_send_update(r, t, wl_fixed_from_double(dx), wl_fixed_from_double(dy), wl_fixed_from_double(scale), wl_fixed_from_double(rotation));
        }
    }
}

void SeatState::handlePinchEnd(bool cancelled) {
    if (!ptrFocus) {
        return;
    }

    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : pinches) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_pinch_v1_send_end(r, serial, t, cancelled);
        }
    }
}

void SeatState::handleHoldBegin(u32 fingers) {
    if (!ptrFocus) {
        return;
    }

    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : holds) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_hold_v1_send_begin(r, serial, t, resOf(ptrFocus), fingers);
        }
    }
}

void SeatState::handleHoldEnd(bool cancelled) {
    if (!ptrFocus) {
        return;
    }

    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : holds) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_hold_v1_send_end(r, serial, t, cancelled);
        }
    }
}

void WaylandImpl::activity() {
    for (IdleNotif* n : idleNotifs) {
        if (n->idled) {
            n->idled = false;
            ext_idle_notification_v1_send_resumed(n->res);
        }

        ev_timer_again(loop, &n->timer);
    }

    if (dpmsSec > 0 && output) {
        ev_timer_again(loop, &dpmsTimer);

        if (dpmsOff) {
            dpmsOff = false;
            output->setPowerSave(true);
            scene->needsFrame = true;
        }
    }
}

void SeatState::handleMotion(double x, double y) {
    curX = x;
    curY = y;
    srv->scene->needsFrame = true;

    if (dragSource) {
        dragMotion();

        return;
    }

    // pointer is over the compositor's own ui: the client sees a leave
    if (srv->scene->ptrCaptured) {
        pointerSetFocus(nullptr, 0, 0);

        return;
    }

    Surface* target = buttonsDown > 0 ? ptrFocus : pickPointerTarget();

    if (target != ptrFocus) {
        double sx = target ? x - target->imgX : 0, sy = target ? y - target->imgY : 0;

        pointerSetFocus(target, sx, sy);

        return;
    }

    if (!ptrFocus) {
        return;
    }

    updateConfineRect();

    double sx = x - ptrFocus->imgX;
    double sy = y - ptrFocus->imgY;
    u32 t = nowMsec();

    for (wl_resource* p : pointers) {
        if (sameClientS(p, ptrFocus)) {
            wl_pointer_send_motion(p, t, wl_fixed_from_double(sx), wl_fixed_from_double(sy));

            if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                wl_pointer_send_frame(p);
            }
        }
    }
}

void SeatState::handleButton(u32 button, bool pressed) {
    srv->scene->needsFrame = true;

    if (!pressed && !contains(pressedButtons, button)) {
        return;
    }

    if (pressed) {
        pressedButtons.pushBack(button);
        lastPressedButton = button;
    } else {
        removeOne(pressedButtons, button);
    }

    if (dragSource) {
        buttonsDown += pressed ? 1 : -1;

        if (!pressed && buttonsDown <= 0) {
            endDrag();
        }

        return;
    }

    if (pressed && buttonsDown == 0) {
        Surface* target = pickPointerTarget();

        if (target != ptrFocus) {
            pointerSetFocus(target, target ? curX - target->imgX : 0, target ? curY - target->imgY : 0);
        }
    }

    if (pressed) {
        for (size_t i = srv->scene->popups.length(); i > 0; i--) {
            Popup* p = srv->scene->popups[i - 1];

            if (!p->mapped || !p->grab) {
                continue;
            }

            Surface* proot = p->surface;

            if (ptrFocus && proot && ptrFocus->rootSurface() == proot) {
                break;
            }

            xdgPopupDismiss(*(PopupImpl*)p);
        }
    }

    if (pressed && ptrFocus) {
        if (Toplevel* t = ptrFocus->rootToplevel()) {
            focusToplevel(t);
        }
    }

    if (ptrFocus) {
        u32 serial = wl_display_next_serial(srv->display);
        u32 t = nowMsec();

        for (wl_resource* p : pointers) {
            if (sameClientS(p, ptrFocus)) {
                wl_pointer_send_button(p, serial, t, button, pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);

                if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                    wl_pointer_send_frame(p);
                }
            }
        }
    }

    buttonsDown += pressed ? 1 : -1;

    if (buttonsDown < 0) {
        buttonsDown = 0;
    }
}

void SeatState::handleScroll(double dx, double dy) {
    srv->scene->needsFrame = true;

    if (!ptrFocus) {
        return;
    }

    u32 t = nowMsec();

    for (wl_resource* p : pointers) {
        if (!sameClientS(p, ptrFocus)) {
            continue;
        }

        bool discrete = wl_resource_get_version(p) >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION;

        if (dy != 0) {
            if (discrete && (i32)dy != 0) {
                wl_pointer_send_axis_discrete(p, WL_POINTER_AXIS_VERTICAL_SCROLL, (i32)dy);
            }

            wl_pointer_send_axis(p, t, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(dy * 15.0));
        }

        if (dx != 0) {
            if (discrete && (i32)dx != 0) {
                wl_pointer_send_axis_discrete(p, WL_POINTER_AXIS_HORIZONTAL_SCROLL, (i32)dx);
            }

            wl_pointer_send_axis(p, t, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(dx * 15.0));
        }

        if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
            wl_pointer_send_frame(p);
        }
    }
}

wl_resource* SeatState::kbTargetRes() {
    if (kbOverride) {
        return resOf(kbOverride);
    }

    if (kbFocus && kbFocus->surface) {
        return resOf(kbFocus->surface);
    }

    return nullptr;
}

void SeatState::kbSendLeave(wl_resource* target) {
    if (!target) {
        return;
    }

    u32 serial = wl_display_next_serial(srv->display);

    for (wl_resource* k : keyboards) {
        if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
            wl_keyboard_send_leave(k, serial, target);
        }
    }
}

void SeatState::kbSendEnter(wl_resource* target) {
    if (!target) {
        return;
    }

    u32 serial = wl_display_next_serial(srv->display);
    wl_array keys;

    wl_array_init(&keys);

    for (u32 kc : pressedKeys) {
        *(u32*)wl_array_add(&keys, sizeof(u32)) = kc;
    }

    for (wl_resource* k : keyboards) {
        if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
            wl_keyboard_send_enter(k, serial, target, &keys);
            wl_keyboard_send_modifiers(k, wl_display_next_serial(srv->display), modsDepressed, modsLatched, modsLocked, modsGroup);
        }
    }

    wl_array_release(&keys);
    if (target) {
        sendSelections(wl_resource_get_client(target));
    }
}

void SeatState::updateModifiers() {
    KeyMods m = kb->mods();
    u32 dep = m.depressed;
    u32 lat = m.latched;
    u32 lock = m.locked;
    u32 grp = m.group;

    if (dep == modsDepressed && lat == modsLatched && lock == modsLocked && grp == modsGroup) {
        return;
    }

    bool groupChanged = grp != modsGroup;

    modsDepressed = dep;
    modsLatched = lat;
    modsLocked = lock;
    modsGroup = grp;

    if (groupChanged) {
        layoutIndicator();
    }

    wl_resource* target = kbTargetRes();

    if (!target) {
        return;
    }

    u32 serial = wl_display_next_serial(srv->display);

    for (wl_resource* k : keyboards) {
        if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
            wl_keyboard_send_modifiers(k, serial, dep, lat, lock, grp);
        }
    }
}

void SeatState::releaseAllKeys() {
    if (pressedKeys.empty()) {
        return;
    }

    if (wl_resource* target = kbTargetRes()) {
        u32 t = nowMsec();

        for (u32 code : pressedKeys) {
            u32 serial = wl_display_next_serial(srv->display);

            for (wl_resource* k : keyboards) {
                if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
                    wl_keyboard_send_key(k, serial, t, code, WL_KEYBOARD_KEY_STATE_RELEASED);
                }
            }
        }
    }

    // note: the shared xkb state is NOT touched here — it mirrors physical
    // keys and is owned by the render-side master; poking it used to strip
    // a physically-held Alt from the modifier mask mid alt-tab
    pressedKeys.clear();
}

void SeatState::handleKey(u32 code, bool pressed) {
    srv->scene->needsFrame = true;

    // never send a release for a press the client did not see
    if (!pressed && !contains(pressedKeys, code)) {
        return;
    }

    if (pressed) {
        pressedKeys.pushBack(code);
    } else {
        removeOne(pressedKeys, code);
    }

    if (wl_resource* target = kbTargetRes()) {
        u32 serial = wl_display_next_serial(srv->display);
        u32 t = nowMsec();

        for (wl_resource* k : keyboards) {
            if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
                wl_keyboard_send_key(k, serial, t, code, pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
            }
        }
    }
}

void SeatState::setSelection(DataSource* src, bool primary) {
    DataSource*& slot = primary ? primarySel : clipboard;

    if (slot == src) {
        return;
    }

    if (slot) {
        if (primary) {
            zwp_primary_selection_source_v1_send_cancelled(slot->res);
        } else {
            wl_data_source_send_cancelled(slot->res);
        }
    }

    slot = src;

    if (wl_resource* target = kbTargetRes()) {
        sendSelections(wl_resource_get_client(target));
    }
}

void SeatState::sendSelections(wl_client* client) {
    for (wl_resource* d : dataDevices) {
        if (wl_resource_get_client(d) != client) {
            continue;
        }

        wl_data_device_send_selection(d, clipboard ? makeOffer(d, clipboard, false) : nullptr);
    }

    for (wl_resource* d : primaryDevices) {
        if (wl_resource_get_client(d) != client) {
            continue;
        }

        zwp_primary_selection_device_v1_send_selection(d, primarySel ? makeOffer(d, primarySel, false) : nullptr);
    }
}

void SeatState::sourceGone(DataSource* src) {
    bool resend = false;

    if (clipboard == src) {
        clipboard = nullptr;
        resend = true;
    }

    if (primarySel == src) {
        primarySel = nullptr;
        resend = true;
    }

    if (dragSource == src) {
        dragSource = nullptr;
        dragTarget = nullptr;
        srv->scene->dragIcon = nullptr;
    }

    if (resend) {
        if (wl_resource* target = kbTargetRes()) {
            sendSelections(wl_resource_get_client(target));
        }
    }
}

void SeatState::startDrag(DataSource* src, Surface* icon) {
    if (buttonsDown <= 0) {
        if (wl_resource_get_version(src->res) >= 3) {
            wl_data_source_send_cancelled(src->res);
        }

        return;
    }

    dragSource = src;
    dragTarget = nullptr;
    srv->scene->dragIcon = icon;
    srv->scene->needsFrame = true;
    pointerSetFocus(nullptr, 0, 0);
    dragMotion();
}

void SeatState::dragMotion() {
    Surface* target = pickPointerTarget();

    if (target != dragTarget) {
        if (dragTarget) {
            for (wl_resource* d : dataDevices) {
                if (sameClientS(d, dragTarget)) {
                    wl_data_device_send_leave(d);
                }
            }
        }

        dragTarget = target;

        if (target) {
            u32 serial = wl_display_next_serial(srv->display);

            for (wl_resource* d : dataDevices) {
                if (!sameClientS(d, target)) {
                    continue;
                }

                wl_resource* offer = makeOffer(d, dragSource, true);

                wl_data_device_send_enter(d, serial, resOf(target), wl_fixed_from_double(curX - target->imgX), wl_fixed_from_double(curY - target->imgY), offer);
            }
        }

        return;
    }

    if (!target) {
        return;
    }

    u32 t = nowMsec();

    for (wl_resource* d : dataDevices) {
        if (sameClientS(d, target)) {
            wl_data_device_send_motion(d, t, wl_fixed_from_double(curX - target->imgX), wl_fixed_from_double(curY - target->imgY));
        }
    }
}

void SeatState::endDrag() {
    DataSource* src = dragSource;

    dragSource = nullptr;
    srv->scene->dragIcon = nullptr;
    srv->scene->needsFrame = true;

    if (dragTarget) {
        for (wl_resource* d : dataDevices) {
            if (sameClientS(d, dragTarget)) {
                wl_data_device_send_drop(d);
            }
        }

        src->dropPerformed = true;

        if (wl_resource_get_version(src->res) >= 3) {
            wl_data_source_send_dnd_drop_performed(src->res);
        }
    } else {
        wl_data_source_send_cancelled(src->res);
    }

    dragTarget = nullptr;
}

void SeatState::focusToplevel(Toplevel* t) {
    if (kbFocus == t) {
        return;
    }

    // the layout belongs to the window: park the current group with the
    // window that loses focus
    if (kbFocus) {
        kbFocus->xkbGroup = kb->mods().group;
    }

    if (kbFocus && kbFocus->surface) {
        auto* old = (ToplevelImpl*)kbFocus;

        if (old->activated) {
            old->activated = false;
            xdgToplevelReconfigure(*old);
        }
    }

    if (t && t->surface) {
        auto* ti = (ToplevelImpl*)t;

        if (!ti->activated) {
            ti->activated = true;
            xdgToplevelReconfigure(*ti);
        }
    }

    if (kbFocus && kbFocus->surface) {
        u32 serial = wl_display_next_serial(srv->display);

        for (wl_resource* k : keyboards) {
            if (sameClient(k, kbFocus)) {
                wl_keyboard_send_leave(k, serial, resOf(kbFocus->surface));
            }
        }
    }

    kbFocus = t;

    if (t) {
        kb->setGroup(t->xkbGroup);

        // refresh the cache silently so kbSendEnter carries fresh modifiers
        KeyMods m = kb->mods();

        modsDepressed = m.depressed;
        modsLatched = m.latched;
        modsLocked = m.locked;
        modsGroup = m.group;
        layoutIndicator();
    }

    if (t && t->surface) {
        kbSendEnter(resOf(t->surface));
        sysO << "imway: focus -> "_sv << (const char*)t->title << endL;
    }
}

void SeatState::popupGrabStart(Popup* p) {
    if (!p->surface) {
        return;
    }

    kbSendLeave(kbTargetRes());
    kbOverride = p->surface;
    kbSendEnter(resOf(kbOverride));
}

void SeatState::popupGone(Popup* p) {
    Surface* s = p->surface;

    if (s && ptrFocus && ptrFocus->rootSurface() == s) {
        ptrFocus = nullptr;
        buttonsDown = 0;
    }

    if (s && kbOverride == s) {
        kbSendLeave(resOf(kbOverride));
        kbOverride = nullptr;
        kbSendEnter(kbTargetRes());
    }
}

void SeatState::surfaceGone(Surface* s) {
    if (ptrFocus == s) {
        ptrFocus = nullptr;
        buttonsDown = 0;
    }
}

void SeatState::toplevelGone(Toplevel* t) {
    if (ptrFocus && ptrFocus->rootToplevel() == t) {
        ptrFocus = nullptr;
        buttonsDown = 0;
    }

    if (kbFocus == t) {
        kbFocus = nullptr;

        for (size_t i = srv->scene->toplevels.length(); i > 0; i--) {
            Toplevel* other = srv->scene->toplevels[i - 1];

            if (other != t && other->mapped) {
                focusToplevel(other);

                break;
            }
        }
    }
}

WaylandImpl::WaylandImpl(ObjPool* p, struct ev_loop* evLoop, Scene& scn, const WaylandConfig& cfg) : pool(p), loop(evLoop), scene(&scn), socketName(cfg.socketName), keyboard(cfg.keyboard), mainDevice(cfg.mainDevice), seat(*this) {
    formats.append(cfg.formats, cfg.formatCount);

    display = wl_display_create();
    STD_VERIFY(display);

    wlLoop = wl_display_get_event_loop(display);

    surfaceAlloc = pool->make<ObjList<SurfaceImpl>>(pool);
    subsurfaceAlloc = pool->make<ObjList<SubsurfaceImpl>>(pool);
    xdgSurfaceAlloc = pool->make<ObjList<XdgSurface>>(pool);
    toplevelAlloc = pool->make<ObjList<ToplevelImpl>>(pool);
    popupAlloc = pool->make<ObjList<PopupImpl>>(pool);
    regionAlloc = pool->make<ObjList<RegionBox>>(pool);
    positionerAlloc = pool->make<ObjList<Positioner>>(pool);
    dmabufBoxAlloc = pool->make<ObjList<BufferBox>>(pool);
    dataSourceAlloc = pool->make<ObjList<DataSource>>(pool);
    spbAlloc = pool->make<ObjList<SpbBox>>(pool);
    dmabufParamsAlloc = pool->make<ObjList<Params>>(pool);
    constraintAlloc = pool->make<ObjList<ConstraintBox>>(pool);
    iconAlloc = pool->make<ObjList<IconBox>>(pool);
    idleAlloc = pool->make<ObjList<IdleNotif>>(pool);
    timelineAlloc = pool->make<ObjList<TimelineBox>>(pool);

    output = cfg.output;
    dpmsSec = cfg.dpmsSec;
    iconPool = cfg.iconPool;
    icons = cfg.iconStore;

    if (icons) {
        icons->setListener(this);
    }
    drmFd = cfg.drmFd;

    if (output && dpmsSec > 0) {
        ev_timer_init(&dpmsTimer, dpmsTimerCb, dpmsSec, dpmsSec);
        dpmsTimer.data = this;
        ev_timer_again(loop, &dpmsTimer);
    }

    if (wl_display_add_socket(display, socketName) != 0) {
        Errno().raise(StringBuilder() << "wl socket "_sv << socketName << " failed (XDG_RUNTIME_DIR?)"_sv);
    }

    wl_display_init_shm(display);
    createGlobals();

    ev_io_init(&wlIo, wlIoCb, wl_event_loop_get_fd(wlLoop), EV_READ);
    wlIo.data = this;
    ev_io_start(loop, &wlIo);

    ev_prepare_init(&flushPrepare, flushCb);
    flushPrepare.data = this;
    ev_prepare_start(loop, &flushPrepare);

    ev_signal_init(&sigInt, signalCb, SIGINT);
    ev_signal_start(loop, &sigInt);
    ev_signal_init(&sigTerm, signalCb, SIGTERM);
    ev_signal_start(loop, &sigTerm);
    watchersStarted = true;

    ev_timer_init(&pingTimer, pingTimerCb, 5., 5.);
    pingTimer.data = this;
    ev_timer_start(loop, &pingTimer);

    sysO << "imway: socket "_sv << socketName << ", output "_sv << scene->outW << "x"_sv << scene->outH << "@"_sv << (i64)scene->hz << endL;
}

WaylandImpl::~WaylandImpl() noexcept {
    ev_timer_stop(loop, &pingTimer);

    if (watchersStarted) {
        ev_io_stop(loop, &wlIo);
        ev_prepare_stop(loop, &flushPrepare);
        ev_signal_stop(loop, &sigInt);
        ev_signal_stop(loop, &sigTerm);
    }

    if (display) {
        wl_display_destroy(display);
        display = nullptr;
    }
}

void WaylandImpl::createGlobals() {
    wl_global_create(display, &wl_compositor_interface, 4, this, compositorBind);
    wl_global_create(display, &wl_subcompositor_interface, 1, this, subcompositorBind);
    wl_global_create(display, &xdg_wm_base_interface, 3, this, wmBaseBind);
    wl_global_create(display, &wl_output_interface, 4, this, outputBind);
    wl_global_create(display, &wl_seat_interface, kSeatVersion, &seat, seatBind);
    wl_global_create(display, &wl_data_device_manager_interface, 3, this, dataManagerBind);
    wl_global_create(display, &zwp_primary_selection_device_manager_v1_interface, 1, this, primaryManagerBind);
    wl_global_create(display, &wp_cursor_shape_manager_v1_interface, 1, this, cursorShapeManagerBind);
    wl_global_create(display, &wp_single_pixel_buffer_manager_v1_interface, 1, this, spbManagerBind);
    wl_global_create(display, &wp_presentation_interface, 1, this, presentationBind);
    wl_global_create(display, &xdg_activation_v1_interface, 1, this, activationBind);
    wl_global_create(display, &zxdg_decoration_manager_v1_interface, 1, this, decoManagerBind);
    wl_global_create(display, &wp_viewporter_interface, 1, this, viewporterBind);
    wl_global_create(display, &zxdg_output_manager_v1_interface, 3, this, xdgOutputManagerBind);
    wl_global_create(display, &wp_fractional_scale_manager_v1_interface, 1, this, fracManagerBind);
    wl_global_create(display, &zwp_relative_pointer_manager_v1_interface, 1, &seat, relPointerManagerBind);
    wl_global_create(display, &zwp_pointer_gestures_v1_interface, 3, &seat, pointerGesturesBind);
    wl_global_create(display, &zwp_pointer_constraints_v1_interface, 1, this, pointerConstraintsBind);
    wl_global_create(display, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1, this, kbInhibitManagerBind);
    wl_global_create(display, &zwp_idle_inhibit_manager_v1_interface, 1, this, idleInhibitManagerBind);
    wl_global_create(display, &xdg_toplevel_icon_manager_v1_interface, 1, this, iconManagerBind);
    wl_global_create(display, &ext_idle_notifier_v1_interface, 1, this, idleNotifierBind);

    u64 syncCap = 0;

    if (drmFd >= 0 && drmGetCap(drmFd, DRM_CAP_SYNCOBJ_TIMELINE, &syncCap) == 0 && syncCap) {
        wl_global_create(display, &wp_linux_drm_syncobj_manager_v1_interface, 1, this, syncManagerBind);
    }

    if (!formats.empty()) {
        int dmabufVersion = 3;

        if (mainDevice) {
            fbTableSize = (u32)(formats.length() * 16);
            fbTableFd = memfd_create("imway-format-table", MFD_CLOEXEC | MFD_ALLOW_SEALING);
            STD_VERIFY(fbTableFd >= 0);

            for (const DmabufFormat& fm : formats) {
                struct {
                    u32 fourcc;
                    u32 pad;
                    u64 modifier;
                } entry = {fm.fourcc, 0, fm.modifier};

                STD_VERIFY(write(fbTableFd, &entry, sizeof(entry)) == sizeof(entry));
            }

            fcntl(fbTableFd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
            dmabufVersion = 4;
        }

        wl_global_create(display, &zwp_linux_dmabuf_v1_interface, dmabufVersion, this, dmabufBind);
    } else {
        sysE << "imway: no dmabuf formats, linux_dmabuf global not created"_sv << endL;
    }
}

bool WaylandImpl::formatSupported(u32 fourcc, u64 modifier) const {
    for (const DmabufFormat& f : formats) {
        if (f.fourcc == fourcc && f.modifier == modifier) {
            return true;
        }
    }

    return false;
}

void WaylandImpl::frameShown(u32 msec) {
    if (scene->focusedToplevel && scene->focusedToplevel != seat.kbFocus) {
        seat.focusToplevel(scene->focusedToplevel);
    }

    for (Toplevel* tl : scene->toplevels) {
        auto* ti = (ToplevelImpl*)tl;

        if (ti->closeRequested) {
            ti->closeRequested = false;

            if (ti->res) {
                xdg_toplevel_send_close(ti->res);
            }
        }
    }

    for (Toplevel* tl : scene->toplevels) {
        if (tl->mapped && tl->surface) {
            fireFrameCallbacks(*(SurfaceImpl*)tl->surface, msec);
        }
    }

    for (Popup* p : scene->popups) {
        if (p->surface) {
            fireFrameCallbacks(*(SurfaceImpl*)p->surface, msec);
        }
    }

    if (scene->dragIcon) {
        fireFrameCallbacks(*(SurfaceImpl*)scene->dragIcon, msec);
    }

    if (scene->cursorSurface) {
        fireFrameCallbacks(*(SurfaceImpl*)scene->cursorSurface, msec);
    }

    for (Toplevel* tl : scene->toplevels) {
        auto* ti = (ToplevelImpl*)tl;

        if (!ti->mapped || !ti->surface || ti->desiredW <= 0) {
            continue;
        }

        bool differsView = ti->desiredW != ti->surface->geomW() || ti->desiredH != ti->surface->geomH();
        bool differsSent = ti->desiredW != ti->cfgW || ti->desiredH != ti->cfgH;

        if (differsView && differsSent) {
            xdgToplevelConfigureSize(*ti, ti->desiredW, ti->desiredH);
        }
    }
}

void WaylandImpl::run() {
    ev_run(loop, 0);

    wl_display_destroy_clients(display);
    wl_display_destroy(display);
    display = nullptr;
}

Wayland* Wayland::create(ObjPool* pool, struct ev_loop* loop, Scene& scene, const WaylandConfig& cfg) {
    return pool->make<WaylandImpl>(pool, loop, scene, cfg);
}
