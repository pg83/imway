
#include "composer.h"
#include "wayland.h"
#include "device_vk.h"
#include "icon.h"
#include "icon_pool.h"
#include "icon_store.h"
#include "icc.h"

#include "input_sink.h"
#include "frame_listener.h"
#include "keyboard.h"
#include "listener.h"
#include "output.h"
#include "intr_list.h"
#include "scene.h"
#include "frame_capture.h"
#include "session.h"
#include "small_obj_allocator.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#include <ev.h>
#include <linux-dmabuf-v1-server-protocol.h>
#include <alpha-modifier-v1-server-protocol.h>
#include <content-type-v1-server-protocol.h>
#include <tearing-control-v1-server-protocol.h>
#include <fifo-v1-server-protocol.h>
#include <commit-timing-v1-server-protocol.h>
#include <ext-image-capture-source-v1-server-protocol.h>
#include <ext-image-copy-capture-v1-server-protocol.h>
#include <wlr-screencopy-unstable-v1-server-protocol.h>
#include <ext-data-control-v1-server-protocol.h>
#include <text-input-unstable-v3-server-protocol.h>
#include <input-method-unstable-v2-server-protocol.h>
#include <virtual-keyboard-unstable-v1-server-protocol.h>
#include <xdg-dialog-v1-server-protocol.h>
#include <pointer-warp-v1-server-protocol.h>
#include <xdg-system-bell-v1-server-protocol.h>
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
#include <color-management-v1-server-protocol.h>
#include <color-representation-v1-server-protocol.h>
#include <xf86drm.h>
#include <xkbcommon/xkbcommon.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

using namespace stl;

namespace {
    struct PopupImpl;
    struct SurfaceImpl;
    struct SubsurfaceImpl;
    struct WaylandImpl;

    struct FifoState;

    struct SurfaceDestroyListener {
        wl_listener listener{};
        SurfaceImpl* surface = nullptr;
    };

    // Parametric color-management image description.  The renderer currently
    // consumes the compact hdr/wide/maxCll/refLum projection; the rest is kept
    // here so protocol descriptions remain lossless and introspectable.
    struct CImgDesc {
        WaylandImpl* srv = nullptr;
        u64 identity = 0;
        bool allowInfo = false;
        ColorDescription color;
        bool ready = true;
    };

    struct CParams {
        WaylandImpl* srv = nullptr;
        CImgDesc d;
        bool tfSet = false;
        bool primSet = false;
        bool lumSet = false;
        bool maxCllSet = false;
        bool maxFallSet = false;
    };

    struct CIcc {
        WaylandImpl* srv = nullptr;
        Vector<u8> data;
        bool fileSet = false;
        bool readFailed = false;
    };
    struct ToplevelImpl;
    struct WaylandImpl;
    struct ConstraintBox;
    struct IdleInhibitor;

    struct CmOutput {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
    };

    struct CmFeedback {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        SurfaceImpl* surface = nullptr;
        wl_listener surfaceDestroy{};
    };

    // refcounted drm syncobj: the resource holds one ref, every commit
    // holding an acquire/release point holds another; the destructor drops
    // the kernel handle when the last one goes
    struct TimelineBox {
        WaylandImpl* srv = nullptr;
        FrameResource* lifetime = nullptr;
        u32 handle = 0;

        ~TimelineBox() noexcept;
    };

    struct DmabufUse;

    struct DmabufUseDestroyListener {
        wl_listener listener{};
        DmabufUse* use = nullptr;
    };

    // Everything tied to one surface use of a dmabuf. The surface and every
    // GPU/KMS frame sampling it reference the containing FrameResource; this
    // destructor is therefore the single release point for both implicit and
    // explicit sync.
    struct DmabufUse {
        WaylandImpl* srv = nullptr;
        DmabufBuffer* buffer = nullptr;
        wl_resource* res = nullptr;
        // wl_surface.get_release callback (v7), fired together with the
        // wl_buffer.release of this buffer
        wl_resource* releaseCb = nullptr;
        DmabufUseDestroyListener destroy;
        TimelineBox* acq = nullptr;
        TimelineBox* rel = nullptr;
        u64 relPoint = 0;

        ~DmabufUse() noexcept;
    };

    struct CommitCache;

    struct CachedBufferDestroyListener {
        wl_listener listener{};
        CommitCache* cache = nullptr;
    };

    struct ActivationTokenRequest: IntrusiveNode {
        WaylandImpl* srv = nullptr;
        wl_client* client = nullptr;
        SurfaceImpl* surface = nullptr;
        StringBuilder appId;
        u32 serial = 0;
        bool serialSet = false;
        bool surfaceSet = false;
        bool committed = false;
    };

    struct ActivationGrant {
        char token[64] = {};
        bool authorized = false;
    };

    struct XdgSurface {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        wl_resource* wmBaseRes = nullptr;
        SurfaceImpl* surface = nullptr;
        ToplevelImpl* toplevel = nullptr;
        PopupImpl* popup = nullptr;
        bool initialConfigureSent = false;
        bool acked = false;

        // one configure in flight: ackedSerial follows ack_configure,
        // committedAckSerial snapshots it on commit — a configure counts as
        // answered once a commit lands with its serial acked
        u32 ackedSerial = 0;
        u32 committedAckSerial = 0;
        Vector<u32> configureSerials;

        RectI pendGeom;
        bool pendGeomSet = false;
    };

    enum class SurfaceRole {
        none,
        xdgToplevel,
        xdgPopup,
        subsurface,
        cursor,
        dragIcon,
    };

    struct SurfaceImpl: public Surface {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        SurfaceRole role = SurfaceRole::none;

        struct {
            wl_resource* buffer = nullptr;
            bool newlyAttached = false;
            SurfaceDestroyListener bufferDestroy;
            bool bufferDestroyArmed = false;
            Vector<wl_resource*> frames;
            // wl_surface.get_release (v7): a callback fired when the buffer
            // committed with this pending state is released
            wl_resource* releaseCb = nullptr;
            bool inputRegionSet = false;
            bool inputRegionChanged = false;
            Vector<RectI> inputRegion;
            bool opaqueRegionSet = false;
            bool opaqueRegionChanged = false;
            Vector<RectI> opaqueRegion;
            RectI damage;
            bool damageAll = false;
            int scale = 0;
            int transform = -1;
            int attachX = 0, attachY = 0;
            // wp-fifo requests are double-buffered like everything else
            bool fifoSet = false;
            bool fifoWait = false;
            // wp-commit-timing target for this commit
            bool timeSet = false;
            u64 timeNs = 0;
        } pending;

        Vector<wl_resource*> frameCbs;
        Vector<wl_resource*> presentFeedbacks;
        Vector<wl_resource*> enteredOutputs;

        DmabufUse* dmabufUse = nullptr;

        wl_resource* vpRes = nullptr;
        double pendSx = -1, pendSy = -1, pendSw = -1, pendSh = -1;
        int pendDw = -1, pendDh = -1;
        bool pendSrcSet = false, pendDstSet = false;

        wl_resource* fracRes = nullptr;
        ConstraintBox* constraint = nullptr;
        wl_resource* kbInhibitRes = nullptr;
        bool kbInhibitActive = false;

        wl_resource* syncRes = nullptr;
        wl_resource* colorRes = nullptr;
        wl_resource* representationRes = nullptr;
        TimelineBox* pendAcqTl = nullptr;
        TimelineBox* pendRelTl = nullptr;
        u64 pendAcqPt = 0, pendRelPt = 0;
        bool pendColorChanged = false;
        // lazily boxed: most surfaces never see color management, and the
        // inline struct is what pushed SurfaceImpl over the allocator limit
        ColorDescription* pendColor = nullptr;
        ColorRepresentation pendRepresentation;
        bool pendRepresentationChanged = false;

        // wp-alpha-modifier surface state
        wl_resource* alphaModRes = nullptr;
        bool pendAlphaChanged = false;
        float pendAlphaMult = 1.f;

        // wp-content-type surface state
        wl_resource* contentTypeRes = nullptr;
        bool pendContentChanged = false;
        u32 pendContentType = 0;

        // wp-fifo / wp-commit-timing state, boxed lazily: most surfaces
        // never touch either, and inline it pushed SurfaceImpl over the
        // small-object cap
        FifoState* fifo = nullptr;

        // wp-tearing-control surface state
        wl_resource* tearingRes = nullptr;
        bool pendTearingChanged = false;
        bool pendTearingAsync = false;

        XdgSurface* xdg = nullptr;
    };




    // one commit's worth of double-buffered state, parked instead of
    // applied: the subsurface sync cache merges successive commits into
    // one of these, a fifo queue entry snapshots exactly one
    struct CommitCache {
        bool valid = false;
        bool hasContent = false;
        int width = 0, height = 0;
        Vector<u8> pixels;
        Vector<wl_resource*> frames;
        DmabufBuffer* dmabuf = nullptr;
        wl_resource* dmabufRes = nullptr;
        CachedBufferDestroyListener dmabufDestroy;
        bool dmabufDestroyArmed = false;
        TimelineBox* acq = nullptr;
        TimelineBox* rel = nullptr;
        u64 acquirePoint = 0, releasePoint = 0;
        bool scaleSet = false, transformSet = false;
        int scale = 1, transform = WL_OUTPUT_TRANSFORM_NORMAL;
        int offsetX = 0, offsetY = 0;
        bool vpSrcSet = false, vpDstSet = false;
        bool vpHasSrc = false, vpHasDst = false;
        double sx = 0, sy = 0, sw = 0, sh = 0;
        int dw = 0, dh = 0;
        bool inputChanged = false, inputSet = false;
        Vector<RectI> inputRegion;
        bool opaqueChanged = false, opaqueSet = false;
        Vector<RectI> opaqueRegion;
        bool colorChanged = false;
        ColorDescription color;
        bool representationChanged = false;
        ColorRepresentation representation;
        // release callback for a synced-subsurface cached buffer (v7)
        wl_resource* releaseCb = nullptr;
        bool alphaChanged = false;
        float alphaMult = 1.f;
    };

    // one queued wp-fifo content update: born at a blocked commit, dies when
    // the frame event admits it (or with its surface). The snapshot rides a
    // CommitCache; the buffer inside is FrameResource-held
    // the fifo object, the commit timer, the barrier condition and the
    // queue of parked updates: one box per surface that uses either protocol
    struct FifoState {
        wl_resource* fifoRes = nullptr;
        wl_resource* commitTimerRes = nullptr;
        bool barrier = false;
        stl::IntrusiveList queue;
    };

    struct FifoEntry: IntrusiveNode {
        CommitCache cache;
        bool setBarrier = false;
        bool waitBarrier = false;
        // wp-commit-timing: do not admit before this CLOCK_MONOTONIC instant
        bool hasTime = false;
        u64 timeNs = 0;
    };

    // ext-image-copy-capture: every box is wire-owned. A session owns at
    // most one live frame; the armed frame is fulfilled from the composed
    // image right after a presentation
    struct CaptureFrame;

    struct CaptureSession: IntrusiveNode {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        CaptureFrame* frame = nullptr;
        // a cursor-source session captures the cursor surface instead
        bool cursor = false;
    };

    struct CaptureBufferDestroyListener {
        wl_listener listener{};
        CaptureFrame* frame = nullptr;
    };

    struct CaptureFrame {
        WaylandImpl* srv = nullptr;
        CaptureSession* session = nullptr;
        wl_resource* res = nullptr;
        wl_resource* buffer = nullptr;
        CaptureBufferDestroyListener bufferDestroy;
        bool bufferDestroyArmed = false;
        bool captured = false;
        bool armed = false;
        int attempts = 0;
    };

    // zwlr-screencopy compat: one-shot frames over the same readback
    struct WlrCopyFrame;

    struct WlrCopyBufferDestroyListener {
        wl_listener listener{};
        WlrCopyFrame* frame = nullptr;
    };

    struct WlrCopyFrame {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        wl_resource* buffer = nullptr;
        WlrCopyBufferDestroyListener bufferDestroy;
        bool bufferDestroyArmed = false;
        int x = 0, y = 0, w = 0, h = 0;
        bool used = false;
        bool armed = false;
        bool withDamage = false;
        int attempts = 0;
    };

    // text-input-v3: the application side, one object per seat per client.
    // The double-buffered relay state is flushed to the input method on
    // commit; enter/leave follow keyboard focus
    struct TextInput: IntrusiveNode {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        wl_client* client = nullptr;
        bool enabled = false;
        bool pendingEnabled = false;
        u32 serial = 0; // count of done events, echoed by the client's commit

        bool surroundingSet = false, pendingSurroundingSet = false;
        StringBuilder surrounding, pendingSurrounding;
        i32 cursor = 0, anchor = 0, pendingCursor = 0, pendingAnchor = 0;

        bool contentSet = false, pendingContentSet = false;
        u32 hint = 0, purpose = 0, pendingHint = 0, pendingPurpose = 0;

        bool rectSet = false, pendingRectSet = false;
        RectI rect, pendingRect;

        u32 changeCause = 0, pendingChangeCause = 0;
    };

    // input-method-v2: the IME side, at most one per seat. commit_string /
    // preedit / delete_surrounding stage here and flush to the active text
    // input on the method's own commit
    struct InputMethod {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        wl_resource* grab = nullptr;
        bool active = false;

        // the input popup surface and its rectangle object (v2)
        SurfaceImpl* popupSurface = nullptr;
        wl_resource* popupRes = nullptr;
        RectI lastRect{-1, -1, -1, -1};

        bool commitSet = false;
        StringBuilder commitStr;
        bool preeditSet = false;
        StringBuilder preeditStr;
        i32 preeditBegin = 0, preeditEnd = 0;
        u32 deleteBefore = 0, deleteAfter = 0;
    };

    struct CaptureCursorSession {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        bool haveSession = false;
        bool entered = false;
        int lastX = -1000000, lastY = -1000000;
        int lastHotX = -1000000, lastHotY = -1000000;
    };

    struct SubsurfaceImpl: public Subsurface {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;

        int pendingX = 0, pendingY = 0;
        bool pendingPos = false;
        bool sync = true;

        CommitCache cache;

        bool effectiveSync() const;
    };

    struct ToplevelImpl: public Toplevel {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        XdgSurface* xdg = nullptr;
        int cfgW = 0, cfgH = 0;
        u32 cfgSerial = 0;
        int prevW = 0, prevH = 0;
        bool cfgDocked = false;
        bool cfgMaximized = false;
        int pendingMinW = 0, pendingMinH = 0;
        int pendingMaxW = 0, pendingMaxH = 0;
        bool pendingMinSet = false, pendingMaxSet = false;
        wl_resource* decoRes = nullptr;
        wl_resource* dialogRes = nullptr;

        // pool icon built from client pixels (xdg-toplevel-icon); wayland
        // owns it: released on replace and on destroy
        Icon* ownIcon = nullptr;
        Icon* pendingOwnIcon = nullptr;
        Icon* pendingIcon = nullptr;
        bool pendingIconFromClient = false;
        bool pendingIconSet = false;
    };

    struct Positioner {
        WaylandImpl* srv = nullptr;

        int w = 0, h = 0;
        int ax = 0, ay = 0, aw = 0, ah = 0;
        u32 anchor = XDG_POSITIONER_ANCHOR_NONE;
        u32 gravity = XDG_POSITIONER_GRAVITY_NONE;
        u32 constraints = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE;
        int dx = 0, dy = 0;
        bool reactive = false;
        int parentW = 0, parentH = 0;
        u32 parentConfigure = 0;

        void place(int& outX, int& outY, int& outW, int& outH,
                   int minX, int minY, int maxX, int maxY) const;
    };

    struct PopupImpl: public Popup {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        XdgSurface* xdg = nullptr;
        int w = 0, h = 0;
        Positioner positioner;
        int pendingX = 0, pendingY = 0, pendingW = 0, pendingH = 0;
        u32 positionSerial = 0;
        bool positionPending = false;
    };

    struct RegionBox {
        WaylandImpl* srv = nullptr;
        Vector<RectI> rects;
    };

    struct IconBox;
    struct IconBufferWatch;

    struct IconBufferDestroyListener {
        wl_listener listener{};
        IconBufferWatch* watch = nullptr;
    };

    struct IconBufferWatch: IntrusiveNode {
        IconBox* icon = nullptr;
        IconBufferDestroyListener destroy;
    };

    // xdg-toplevel-icon: pixels are copied out of the wl_shm buffer at
    // add_buffer time, while destroy listeners enforce the protocol's source
    // buffer lifetime until the icon object itself dies.
    //
    // explicit limits: adds beyond them are ignored, not errors — the icon
    // then resolves through the name/store path
    constexpr int kIconMaxSize = 1024;
    constexpr int kIconMaxBuffers = 64;

    struct IconBox {
        WaylandImpl* srv = nullptr;
        wl_resource* res = nullptr;
        StringBuilder name;
        Vector<u32> pixels;
        IntrusiveList bufferWatches;
        int bufferWatchCount = 0;
        int w = 0, h = 0;
        int effectiveSize = 0;
        bool immutable = false;
    };

    struct ConstraintBox {
        WaylandImpl* srv = nullptr;
        SurfaceImpl* surface = nullptr;
        wl_resource* res = nullptr;
        bool isLock = false;
        bool oneshot = false;
        bool dead = false;
        bool hasRegion = false;
        Vector<RectI> region;
        bool pendingSet = false;
        bool pendingHasRegion = false;
        Vector<RectI> pendingRegion;
    };

    struct IdleInhibitor: IntrusiveNode {
        WaylandImpl* srv = nullptr;
        SurfaceImpl* surface = nullptr;
        wl_resource* res = nullptr;
    };

    struct BufferBox {
        WaylandImpl* srv = nullptr;
        DmabufBuffer* buf = nullptr;

        ~BufferBox() noexcept {
            dmabufUnref(buf);
        }
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
        // the resource is an ext-data-control source: send/cancelled route
        // through the dc interface, everything else is shared
        bool dc = false;
        Vector<Mime> mimes;
        IntrusiveList offers;
        u32 dndActions = 0;
        bool dropPerformed = false;
        bool actionsSet = false;
        bool usedForDrag = false;
        bool usedForSelection = false;
    };

    // the node links it into its DataSource's offer list
    struct Offer: IntrusiveNode {
        WaylandImpl* srv = nullptr;
        DataSource* source = nullptr;
        wl_resource* res = nullptr;
        // the offer resource speaks ext-data-control
        bool dc = false;
        bool dnd = false;
        bool accepted = false;
        bool finished = false;
        u32 action = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
    };

    struct SeatState {
        WaylandImpl* srv = nullptr;

        Vector<wl_resource*> keyboards;
        Vector<wl_resource*> pointers;
        Vector<wl_resource*> dataDevices;
        Vector<wl_resource*> primaryDevices;
        Vector<wl_resource*> dcDevices;
        stl::IntrusiveList textInputs;
        InputMethod* inputMethod = nullptr;
        Vector<wl_resource*> relPointers;
        Vector<wl_resource*> swipes;
        Vector<wl_resource*> pinches;
        Vector<wl_resource*> holds;
        Vector<wl_resource*> activeSwipes;
        Vector<wl_resource*> activePinches;
        Vector<wl_resource*> activeHolds;

        ConstraintBox* activeConstraint = nullptr;

        DataSource* clipboard = nullptr;
        DataSource* primarySel = nullptr;

        DataSource* dragSource = nullptr;
        wl_client* dragClient = nullptr;
        Surface* dragTarget = nullptr;
        u32 lastPressedButton = 0;
        Vector<u32> pressedButtons;

        struct InputSerial {
            u32 value = 0;
            wl_client* client = nullptr;
            Surface* surface = nullptr;
            u64 focusGeneration = 0;
        };

        Vector<InputSerial> inputSerials;
        // Input serials authorizing selection/activation are valid only for
        // the effective keyboard-focus epoch in which they were delivered.
        // Otherwise a client can retain a serial, lose focus, regain it much
        // later and replay the old authority.
        u64 focusGeneration = 1;
        u32 pointerGrabSerial = 0;
        wl_client* pointerGrabClient = nullptr;
        Surface* pointerGrabOrigin = nullptr;

        struct PointerEnter {
            wl_resource* pointer = nullptr;
            u32 serial = 0;
        };

        Vector<PointerEnter> pointerEnters;

        struct CursorShapeBinding {
            wl_resource* device = nullptr;
            wl_resource* pointer = nullptr;
        };

        Vector<CursorShapeBinding> cursorShapeBindings;

        // last delivered key press: a valid xdg_popup.grab trigger too
        // (menu key, shift+F10)
        u32 keyGrabSerial = 0;
        wl_client* keyGrabClient = nullptr;
        u64 keyGrabGeneration = 0;

        void releaseAllKeys();
        void rememberSerial(u32 serial, wl_client* client, Surface* surface);
        bool validSerial(wl_client* client, u32 serial) const;
        void rememberPointerEnter(wl_resource* pointer, u32 serial);
        bool validPointerEnter(wl_resource* pointer, u32 serial) const;
        void forgetPointer(wl_resource* pointer);
        bool validCursorShapeEnter(wl_resource* device, u32 serial) const;
        void forgetCursorShapeDevice(wl_resource* device);
        bool validSelectionSerial(wl_client* client, u32 serial);
        void setSelection(wl_client* client, u32 serial, DataSource* src, bool primary);
        void applySelection(DataSource* src, bool primary);
        void sendDcSelections();
        void sendSelections(wl_client* client);
        void sourceGone(DataSource* src);
        void startDrag(wl_client* client, u32 serial, DataSource* src, Surface* origin, Surface* icon);
        void dragMotion();
        void endDrag();

        Keyboard* kb = nullptr;
        bool uiCaptured = false;

        Toplevel* kbFocus = nullptr;
        // nested menu chains grab in a stack; kbOverride mirrors its top so
        // dismissing the deepest popup returns the keyboard to its parent
        // popup, not straight to the toplevel
        Surface* kbOverride = nullptr;
        IntrusiveList grabStack; // GrabNode links
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
        void handleScroll(const ScrollEvent& ev);
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
        bool updateConfineRegion();

        wl_resource* kbTargetRes();
        TextInput* activeTextInput();
        void imUpdateActivation();
        void imFlushToTextInput();
        bool imGrabActive() const;
        void imUpdatePopup();
        Surface* kbFocusSurface();
        void kbSendLeave(wl_resource* target);
        void kbSendEnter(wl_resource* target);
        void updateModifiers();
        void layoutIndicator();
        void updateShortcutInhibit();

        void focusToplevel(Toplevel* t);
        void toplevelUnmapped(Toplevel* t);
        void popupGrabStart(Popup* p);
        void grabGone(Surface* s, bool sendLeave);
        void popupGone(Popup* p);
        void surfaceGone(Surface* s);
        void toplevelGone(Toplevel* t);
    };

    struct CallIconsReloaded: Listener {
        WaylandImpl* parent;

        CallIconsReloaded(WaylandImpl* p);
        void onListen(void*) override;
    };

    struct CallWaylandSessionEnabled: Listener {
        WaylandImpl* parent;

        CallWaylandSessionEnabled(WaylandImpl* p);
        void onListen(void*) override;
    };

    struct CallWaylandSessionDisabled: Listener {
        WaylandImpl* parent;

        CallWaylandSessionDisabled(WaylandImpl* p);
        void onListen(void*) override;
    };

    struct WaylandImpl: public Wayland, public InputSink, public Listener {
        Composer* composer = nullptr;
        ObjPool* pool = nullptr;
        SmallObjAllocator* alloc = nullptr;
        struct ev_loop* loop = nullptr;
        Scene* scene = nullptr;
        wl_display* display = nullptr;
        wl_event_loop* wlLoop = nullptr;

        StringView socketName;
        Keyboard* keyboard = nullptr;
        Vector<DmabufFormat> formats;
        Vector<u16> scanoutIndices;
        u64 mainDevice = 0;
        int fbTableFd = -1;
        u32 fbTableSize = 0;
        u64 tokenCounter = 0;

        struct WmBasePing {
            wl_resource* res = nullptr;
            bool acked = true;
        };

        Vector<WmBasePing> wmBases;
        Vector<wl_resource*> outputResources;
        ev_timer pingTimer{};
        u32 pingSerial = 0;

        SeatState seat;

        ev_io wlIo{};
        ev_prepare flushPrepare{};
        ev_signal sigInt{}, sigTerm{};
        bool watchersStarted = false;

        u64 nextToplevelId = 1;

        IntrusiveList activationTokenRequests;
        Vector<ActivationGrant> activationGrants;
        IconPool* iconPool = nullptr;
        IconStore* icons = nullptr;

        // color-management-v1 objects
        Vector<CmOutput*> cmOutputResources;
        Vector<CmFeedback*> cmFeedbackResources;
        u64 cimgIdentity = 0;
        u64 cmDisplayIdentity = 0;
        OutputColorState cmDisplayColor;

        int drmFd = -1;
        bool explicitSyncSupported = false;

        struct IdleNotif: IntrusiveNode {
            WaylandImpl* srv = nullptr;
            wl_resource* res = nullptr;
            bool idled = false;
            // v2 get_input_idle_notification: tracks raw input idleness and
            // ignores idle inhibitors
            bool ignoreInhibitors = false;
            ev_timer timer{};
        };

        IntrusiveList idleNotifs;
        IntrusiveList idleInhibitors;
        IntrusiveList captureSessions;
        Vector<CaptureCursorSession*> cursorSessions;
        Vector<WlrCopyFrame*> screencopyFrames;
        ::Output* output = nullptr;
        double dpmsSec = 0;
        bool dpmsOff = false;
        ev_timer dpmsTimer{};

        void activity();
        bool idleBlocked();

        WaylandImpl(Composer& comp, const WaylandConfig& cfg);
        ~WaylandImpl() noexcept;

        void run() override;

        void iconsReloaded();
        void syncKeyboardCapture();
        void sessionEnabled();
        void sessionDisabled();
        bool pointerMotion(PointerMotionEvent& ev) override;
        bool button(u32 btn, bool pressed) override;
        bool key(u32 code, bool pressed) override;
        bool scroll(const ScrollEvent& ev) override;
        bool swipeBegin(u32 fingers) override;
        bool swipeUpdate(double dx, double dy) override;
        bool swipeEnd(bool cancelled) override;
        bool pinchBegin(u32 fingers) override;
        bool pinchUpdate(double dx, double dy, double scale, double rotation) override;
        bool pinchEnd(bool cancelled) override;
        bool holdBegin(u32 fingers) override;
        bool holdEnd(bool cancelled) override;

        void onListen(void* arg) override;
        void syncColorState();

        bool formatSupported(u32 fourcc, u64 modifier) const;
        void createGlobals();
    };

    CallIconsReloaded::CallIconsReloaded(WaylandImpl* p)
        : parent(p)
    {
    }

    void CallIconsReloaded::onListen(void*) {
        parent->iconsReloaded();
    }

    CallWaylandSessionEnabled::CallWaylandSessionEnabled(WaylandImpl* p)
        : parent(p)
    {
    }

    void CallWaylandSessionEnabled::onListen(void*) {
        parent->sessionEnabled();
    }

    CallWaylandSessionDisabled::CallWaylandSessionDisabled(WaylandImpl* p)
        : parent(p)
    {
    }

    void CallWaylandSessionDisabled::onListen(void*) {
        parent->sessionDisabled();
    }

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

    u32 sendXdgSurfaceConfigure(XdgSurface& xs) {
        u32 serial = wl_display_next_serial(xs.srv->display);

        xs.configureSerials.pushBack(serial);
        xdg_surface_send_configure(xs.res, serial);

        return serial;
    }

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

        forEach<Surface, SceneNode>(srv->scene->surfaces, [&](Surface& surface) {
            auto& impl = (SurfaceImpl&)surface;

            if (impl.xdg && impl.xdg->wmBaseRes == res) {
                impl.xdg->wmBaseRes = nullptr;
            }
        });

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
                // a software "now" timestamp is not vblank-locked; the spec
                // says VSYNC must not be claimed without hardware timing
                flags = 0;
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

        forEach<Subsurface>(s.stackBelow, [&](Subsurface& c) {
            if (c.surface) {
                fireFrameCallbacks(*(SurfaceImpl*)c.surface, t);
            }
        });

        forEach<Subsurface>(s.stackAbove, [&](Subsurface& c) {
            if (c.surface) {
                fireFrameCallbacks(*(SurfaceImpl*)c.surface, t);
            }
        });
    }

    void resDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void unlinkFromParent(SubsurfaceImpl&);
    void applyCache(SurfaceImpl&, CommitCache&);
    void drainFifo(SurfaceImpl&, bool presented);
    void fifoSurfaceGone(SurfaceImpl&);
    void applySubsurfaceCache(SubsurfaceImpl&);
    void xdgHandleCommit(SurfaceImpl&);
    void xdgPopupDismiss(PopupImpl&);
    void dismissPopupTree(PopupImpl&);
    void viewportApplyPending(SurfaceImpl&);
    void viewportSurfaceGone(SurfaceImpl&);
    void alphaModSurfaceGone(SurfaceImpl&);
    void contentTypeSurfaceGone(SurfaceImpl&);
    void tearingSurfaceGone(SurfaceImpl&);
    void fracSurfaceGone(SurfaceImpl&);
    void constraintSurfaceGone(SurfaceImpl&);
    void constraintApplyPending(SurfaceImpl&);
    void kbInhibitSurfaceGone(SurfaceImpl&);
    void syncSurfaceGone(SurfaceImpl&);
    DmabufBuffer* dmabufFromRes(wl_resource*);
    struct SpbBox;
    SpbBox* spbFromRes(wl_resource*);

    SurfaceImpl* surfaceFrom(wl_resource* res) {
        return (SurfaceImpl*)wl_resource_get_user_data(res);
    }

    // fire and consume a get_release callback (v7): the buffer it was
    // associated with has been released
    static void fireReleaseCb(wl_resource*& cb) {
        if (cb) {
            wl_callback_send_done(cb, 0);
            wl_resource_destroy(cb);
            cb = nullptr;
        }
    }

    // drop a release callback that never reached a committed buffer
    static void dropReleaseCb(wl_resource*& cb) {
        if (cb) {
            wl_resource_destroy(cb);
            cb = nullptr;
        }
    }

    void detachPendingBuffer(SurfaceImpl& s) {
        if (s.pending.bufferDestroyArmed) {
            wl_list_remove(&s.pending.bufferDestroy.listener.link);
            s.pending.bufferDestroyArmed = false;
        }

        s.pending.buffer = nullptr;
    }

    void pendingBufferDestroyed(wl_listener* l, void*) {
        SurfaceImpl* s = ((SurfaceDestroyListener*)l)->surface;

        s->pending.buffer = nullptr;
        s->pending.bufferDestroyArmed = false;
        wl_list_remove(&s->pending.bufferDestroy.listener.link);
    }

    void tlRef(TimelineBox* t) {
        if (t) {
            frameRef(t->lifetime);
        }
    }

    void tlUnref(TimelineBox* t) {
        if (t) {
            frameUnref(t->lifetime);
        }
    }

    void cachedDmabufDestroyed(wl_listener* l, void*) {
        CommitCache* cache = ((CachedBufferDestroyListener*)l)->cache;

        cache->dmabufRes = nullptr;
        cache->dmabufDestroyArmed = false;
        wl_list_remove(&cache->dmabufDestroy.listener.link);
    }

    void releaseCachedDmabuf(WaylandImpl* srv, CommitCache& cache) {
        if (!cache.dmabuf) {
            return;
        }

        if (cache.rel) {
            drmSyncobjTimelineSignal(srv->drmFd, &cache.rel->handle, &cache.releasePoint, 1);
        }

        tlUnref(cache.acq);
        tlUnref(cache.rel);

        if (cache.dmabufRes) {
            wl_buffer_send_release(cache.dmabufRes);
        }

        // the cached buffer is dropped before being applied: its release
        // callback fires now
        fireReleaseCb(cache.releaseCb);

        if (cache.dmabufDestroyArmed) {
            wl_list_remove(&cache.dmabufDestroy.listener.link);
        }

        dmabufUnref(cache.dmabuf);
        cache.dmabuf = nullptr;
        cache.dmabufRes = nullptr;
        cache.dmabufDestroyArmed = false;
        cache.acq = cache.rel = nullptr;
    }

    void dmabufUseBufferDestroyed(wl_listener* l, void*) {
        auto* use = ((DmabufUseDestroyListener*)l)->use;

        use->res = nullptr;
        wl_list_remove(&use->destroy.listener.link);
    }

    void releaseHeldDmabuf(SurfaceImpl& s) {
        if (!s.dmabuf) {
            return;
        }

        FrameResource* frame = s.frame;

        s.texture = nullptr;
        s.dmabuf = nullptr;
        s.frame = nullptr;
        s.dmabufUse = nullptr;
        s.syncAcquireWait = false;
        s.explicitSync = false;
        frameUnref(frame);
    }

    DmabufUse* holdDmabuf(SurfaceImpl& s, wl_resource* buffer, DmabufBuffer* buf, bool addRef = true) {
        releaseHeldDmabuf(s);

        FrameResource* frame = frameCreate();
        DmabufUse* use = frame->make<DmabufUse>();

        if (addRef) {
            dmabufRef(buf);
        }

        use->srv = s.srv;
        use->buffer = buf;
        use->res = buffer;
        use->destroy.listener.notify = dmabufUseBufferDestroyed;
        use->destroy.use = use;

        if (buffer) {
            wl_resource_add_destroy_listener(buffer, &use->destroy.listener);
        }

        s.frame = frame;
        s.dmabuf = buf;
        s.dmabufUse = use;

        return use;
    }

    void surfaceDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void surfaceAttach(wl_client*, wl_resource* res, wl_resource* buffer, i32 x, i32 y) {
        SurfaceImpl& s = *surfaceFrom(res);

        // v5 split the attach offset into wl_surface.offset; attaching with a
        // non-zero offset is a protocol error there
        if ((x || y) && wl_resource_get_version(res) >= WL_SURFACE_OFFSET_SINCE_VERSION) {
            wl_resource_post_error(res, WL_SURFACE_ERROR_INVALID_OFFSET,
                                   "attach offset must be zero since version 5");

            return;
        }

        detachPendingBuffer(s);
        s.pending.buffer = buffer;
        s.pending.newlyAttached = true;
        s.pending.attachX = x;
        s.pending.attachY = y;

        if (buffer) {
            s.pending.bufferDestroy.listener.notify = pendingBufferDestroyed;
            s.pending.bufferDestroy.surface = &s;
            wl_resource_add_destroy_listener(buffer, &s.pending.bufferDestroy.listener);
            s.pending.bufferDestroyArmed = true;
        }
    }

    void surfaceDamage(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        SurfaceImpl& s = *surfaceFrom(res);

        if (s.vp.hasSrc || s.vp.hasDst || s.bufferTransform != WL_OUTPUT_TRANSFORM_NORMAL) {
            s.pending.damageAll = true;

            return;
        }

        i32 sc = s.bufferScale;
        i64 bx = (i64)x * sc, by = (i64)y * sc;
        i64 bw = (i64)w * sc, bh = (i64)h * sc;
        constexpr i64 minI32 = -0x80000000ll, maxI32 = 0x7fffffff;

        if (bx < minI32 || bx > maxI32 || by < minI32 || by > maxI32 || bw < minI32 || bw > maxI32 || bh < minI32 || bh > maxI32) {
            s.pending.damageAll = true;

            return;
        }

        unionRect(s.pending.damage, {(i32)bx, (i32)by, (i32)bw, (i32)bh});
    }

    void frameCallbackDestroyed(wl_resource* cb) {
        SurfaceImpl* s = (SurfaceImpl*)wl_resource_get_user_data(cb);

        if (!s) {
            return;
        }

        removeOne(s->pending.frames, cb);
        removeOne(s->frameCbs, cb);

        if (s->sub) {
            removeOne(((SubsurfaceImpl*)s->sub)->cache.frames, cb);
        }

        if (s->fifo) {
            forEach<FifoEntry>(s->fifo->queue, [cb](FifoEntry& e) {
                removeOne(e.cache.frames, cb);
            });
        }
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

    void surfaceSetOpaqueRegion(wl_client*, wl_resource* res, wl_resource* region) {
        SurfaceImpl& s = *surfaceFrom(res);

        s.pending.opaqueRegionChanged = true;

        if (!region) {
            s.pending.opaqueRegionSet = false;
            s.pending.opaqueRegion.clear();

            return;
        }

        auto* box = (RegionBox*)wl_resource_get_user_data(region);

        s.pending.opaqueRegionSet = true;
        s.pending.opaqueRegion.clear();
        s.pending.opaqueRegion.append(box->rects.begin(), box->rects.length());
    }

    void surfaceSetInputRegion(wl_client*, wl_resource* res, wl_resource* region) {
        SurfaceImpl& s = *surfaceFrom(res);

        s.pending.inputRegionChanged = true;

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

        // libwayland validates stride against the pixel width only; a client
        // can pass stride < w*4 and walk the copy loop past the mmap
        if (stride < (i64)w * 4) {
            sysE << "imway: shm stride "_sv << stride << " < width*4"_sv << endL;
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

        // the renderer samples the fourth byte as alpha; XRGB leaves it
        // undefined and clients commonly write zero there
        if (fmt == WL_SHM_FORMAT_XRGB8888) {
            i32 y0 = incremental ? rect->y : 0;
            i32 y1 = incremental ? rect->y + rect->h : h;
            i32 x0 = incremental ? rect->x : 0;
            i32 x1 = incremental ? rect->x + rect->w : w;

            for (i32 y = y0; y < y1; y++) {
                u8* row = out.mutData() + ((size_t)y * w + x0) * 4;

                for (i32 x = x0; x < x1; x++, row += 4) {
                    row[3] = 0xff;
                }
            }
        }
    }

    void copyShmBuffer(SurfaceImpl& s, wl_shm_buffer* shm, const RectI* rect) {
        copyShmBufferTo(*shm, s.width, s.height, s.pixels, rect);

        if (s.width > 0) {
            s.dirty = true;
            s.hasContent = true;
        }
    }

    void applyChildrenCaches(SurfaceImpl& s) {
        IntrusiveList* stacks[] = {&s.stackBelow, &s.stackAbove};

        for (auto* stack : stacks) {
            forEach<Subsurface>(*stack, [&](Subsurface& c) {
                SubsurfaceImpl& sub = impl(&c);

                if (sub.pendingPos) {
                    sub.x = sub.pendingX;
                    sub.y = sub.pendingY;
                    sub.pendingPos = false;
                }

                if (sub.sync) {
                    applySubsurfaceCache(sub);
                }
            });
        }
    }

    void applyColorState(Surface& s, const ColorDescription& color) {
        s.color = color;
    }

    void applyCache(SurfaceImpl& s, CommitCache& cache) {

        if (cache.scaleSet) {
            s.bufferScale = cache.scale;
            cache.scaleSet = false;
            s.damageAll = true;
        }

        if (cache.transformSet) {
            s.bufferTransform = cache.transform;
            cache.transformSet = false;
            s.damageAll = true;
        }

        if (cache.colorChanged) {
            applyColorState(s, cache.color);
            cache.colorChanged = false;
        }

        if (cache.representationChanged) {
            s.representation = cache.representation;
            cache.representationChanged = false;
        }

        s.bufferOffsetX = satAddI32(s.bufferOffsetX, cache.offsetX);
        s.bufferOffsetY = satAddI32(s.bufferOffsetY, cache.offsetY);
        cache.offsetX = cache.offsetY = 0;

        if (cache.vpSrcSet) {
            s.vp.hasSrc = cache.vpHasSrc;
            s.vp.sx = cache.sx;
            s.vp.sy = cache.sy;
            s.vp.sw = cache.sw;
            s.vp.sh = cache.sh;
            cache.vpSrcSet = false;
        }

        if (cache.vpDstSet) {
            s.vp.hasDst = cache.vpHasDst;
            s.vp.dw = cache.dw;
            s.vp.dh = cache.dh;
            cache.vpDstSet = false;
        }

        if (cache.valid) {
            releaseHeldDmabuf(s);
            s.hasContent = cache.hasContent;
            s.width = cache.width;
            s.height = cache.height;

            if (cache.dmabuf) {
                if (cache.dmabufDestroyArmed) {
                    wl_list_remove(&cache.dmabufDestroy.listener.link);
                    cache.dmabufDestroyArmed = false;
                }

                DmabufUse* use = holdDmabuf(s, cache.dmabufRes, cache.dmabuf, false);

                // the cached release callback follows the buffer into the
                // applied use
                use->releaseCb = cache.releaseCb;
                cache.releaseCb = nullptr;
                use->acq = cache.acq;
                use->rel = cache.rel;
                use->relPoint = cache.releasePoint;
                s.syncAcquireHandle = use->acq ? use->acq->handle : 0;
                s.syncAcquirePoint = cache.acquirePoint;
                s.syncAcquireWait = use->acq != nullptr;
                s.explicitSync = use->acq != nullptr;
                s.pixels.clear();
                s.dirty = true;

                cache.dmabuf = nullptr;
                cache.dmabufRes = nullptr;
                cache.acq = cache.rel = nullptr;
            } else if (cache.hasContent && !cache.pixels.empty()) {
                s.pixels.xchg(cache.pixels);
                s.dirty = true;
                s.damageAll = true;
            }

            cache.pixels.clear();
            cache.valid = false;
        }

        if (cache.inputChanged) {
            s.inputRegionSet = cache.inputSet;
            s.inputRegion.clear();

            for (const RectI& cachedRect : cache.inputRegion) {
                RectI r = cachedRect;

                clipRect(r, s.viewW(), s.viewH());

                if (!r.empty()) {
                    s.inputRegion.pushBack(r);
                }
            }

            cache.inputRegion.clear();
            cache.inputChanged = false;
        }

        if (cache.opaqueChanged) {
            s.opaqueRegionSet = cache.opaqueSet;
            s.opaqueRegion.clear();
            s.opaqueRegion.append(cache.opaqueRegion.begin(), cache.opaqueRegion.length());
            cache.opaqueRegion.clear();
            cache.opaqueChanged = false;
        }

        if (cache.alphaChanged) {
            s.alphaMult = cache.alphaMult;
            cache.alphaChanged = false;
        }

        constraintApplyPending(s);

        for (wl_resource* cb : cache.frames) {
            s.frameCbs.pushBack(cb);
        }

        cache.frames.clear();
    }

    void applySubsurfaceCache(SubsurfaceImpl& sub) {
        SurfaceImpl& s = *(SurfaceImpl*)sub.surface;

        applyCache(s, sub.cache);

        if (sub.pendingPos) {
            sub.x = sub.pendingX;
            sub.y = sub.pendingY;
            sub.pendingPos = false;
        }

        forEach<Subsurface>(sub.surface->stackBelow, [&](Subsurface& c) {
            if (impl(&c).sync) {
                applySubsurfaceCache(impl(&c));
            }
        });

        forEach<Subsurface>(sub.surface->stackAbove, [&](Subsurface& c) {
            if (impl(&c).sync) {
                applySubsurfaceCache(impl(&c));
            }
        });
    }

    u64 nowNs() {
        timespec ts{};

        clock_gettime(CLOCK_MONOTONIC, &ts);

        return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
    }

    void drainFifo(SurfaceImpl& s, bool presented) {
        if (presented) {
            // this surface's content was just shown: its barrier is satisfied
            s.fifo->barrier = false;
        }

        u64 now = nowNs();

        while (!s.fifo->queue.empty()) {
            FifoEntry* e = (FifoEntry*)s.fifo->queue.mutFront();

            // an invisible surface cannot hold a barrier (the spec's escape
            // hatch against stalled clients): ignore barriers entirely. A
            // commit-timing target holds either way: it expires by itself
            if ((presented && e->waitBarrier && s.fifo->barrier) ||
                (e->hasTime && e->timeNs > now)) {
                break;
            }

            e->unlink();
            applyCache(s, e->cache);
            applyChildrenCaches(s);

            if (s.xdg) {
                xdgHandleCommit(s);
            }

            if (e->setBarrier) {
                s.fifo->barrier = true;
            }

            s.srv->alloc->release(e);
            s.srv->scene->needsFrame = true;
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

        // references move from pending into the content-use arena and are
        // released when the last GPU/KMS frame using that content retires
        s.dmabufUse->acq = s.pendAcqTl;
        s.dmabufUse->rel = s.pendRelTl;
        s.dmabufUse->relPoint = s.pendRelPt;
        s.syncAcquireHandle = s.dmabufUse->acq->handle;
        s.syncAcquirePoint = s.pendAcqPt;
        s.syncAcquireWait = true;
        s.explicitSync = true;
        s.pendAcqTl = s.pendRelTl = nullptr;
    }

    void surfaceCommit(wl_client*, wl_resource* res) {
        SurfaceImpl& s = *surfaceFrom(res);

        s.srv->scene->needsFrame = true;

        if (s.xdg && !s.xdg->toplevel && !s.xdg->popup) {
            wl_resource_post_error(s.xdg->res, XDG_SURFACE_ERROR_NOT_CONSTRUCTED, "xdg_surface committed before constructing a role object");

            return;
        }

        if (s.xdg && s.pending.newlyAttached && s.pending.buffer && (!s.xdg->initialConfigureSent || !s.xdg->acked)) {
            wl_resource_post_error(s.xdg->res, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER, "buffer attached before the initial configure was acknowledged");

            return;
        }

        SubsurfaceImpl* sub = (SubsurfaceImpl*)s.sub;
        bool toCache = sub && sub->effectiveSync();
        CommitCache* cache = toCache ? &sub->cache : nullptr;

        // wp-fifo: while older updates sit queued (ordering) or this update
        // waits on a raised barrier, the commit parks in a queue entry
        // instead of applying; the frame event admits one entry per
        // presentation. The entry joins the queue immediately, so an error
        // return mid-commit leaves it owned by the surface
        bool fifoSet = s.pending.fifoSet;
        bool fifoWait = s.pending.fifoWait;
        bool timeSet = s.pending.timeSet;
        u64 timeNs = s.pending.timeNs;

        s.pending.fifoSet = false;
        s.pending.fifoWait = false;
        s.pending.timeSet = false;
        s.pending.timeNs = 0;

        if (!cache && s.fifo && (!s.fifo->queue.empty() || (fifoWait && s.fifo->barrier) ||
                       (timeSet && timeNs > nowNs()))) {
            FifoEntry* entry = s.srv->alloc->make<FifoEntry>();

            entry->setBarrier = fifoSet;
            entry->waitBarrier = fifoWait;
            entry->hasTime = timeSet;
            entry->timeNs = timeNs;
            s.fifo->queue.pushBack(entry);
            cache = &entry->cache;
        } else if (fifoSet && s.fifo) {
            // applied in this commit: the barrier goes up with the content
            s.fifo->barrier = true;
        }

        if (s.syncRes && !syncCommitOk(s)) {
            return;
        }

        bool cachedContent = cache && cache->valid ? cache->hasContent : s.hasContent;
        bool validateCurrentSize = !s.pending.newlyAttached && cachedContent && (s.pending.scale > 0 || s.pending.transform >= 0);

        if ((s.pending.newlyAttached && s.pending.buffer) || validateCurrentSize) {
            int bw = validateCurrentSize ? (cache && cache->valid ? cache->width : s.width) : 0;
            int bh = validateCurrentSize ? (cache && cache->valid ? cache->height : s.height) : 0;

            if (!validateCurrentSize) {
                if (wl_shm_buffer* shm = wl_shm_buffer_get(s.pending.buffer)) {
                    bw = wl_shm_buffer_get_width(shm);
                    bh = wl_shm_buffer_get_height(shm);
                } else if (SpbBox* spb = spbFromRes(s.pending.buffer)) {
                    (void)spb;
                    bw = bh = 1;
                } else if (DmabufBuffer* db = dmabufFromRes(s.pending.buffer)) {
                    bw = db->width;
                    bh = db->height;
                }
            }

            int scale = s.pending.scale > 0 ? s.pending.scale : cache && cache->scaleSet ? cache->scale : s.bufferScale;
            int transform = s.pending.transform >= 0 ? s.pending.transform : cache && cache->transformSet ? cache->transform : s.bufferTransform;
            bool swapped = transform == WL_OUTPUT_TRANSFORM_90 || transform == WL_OUTPUT_TRANSFORM_270 ||
                           transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 || transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
            int tw = swapped ? bh : bw, th = swapped ? bw : bh;

            if (bw > 0 && (tw % scale != 0 || th % scale != 0)) {
                wl_resource_post_error(res, WL_SURFACE_ERROR_INVALID_SIZE, "buffer dimensions are not divisible by buffer_scale after transform");

                return;
            }
        }

        if (s.pending.newlyAttached) {
            if (s.pending.buffer) {
                if (cache) {
                    cache->offsetX = satAddI32(cache->offsetX, s.pending.attachX);
                    cache->offsetY = satAddI32(cache->offsetY, s.pending.attachY);
                } else {
                    s.bufferOffsetX = satAddI32(s.bufferOffsetX, s.pending.attachX);
                    s.bufferOffsetY = satAddI32(s.bufferOffsetY, s.pending.attachY);
                }
            }

            if (cache) {
                releaseCachedDmabuf(s.srv, *cache);
            }

            if (!s.pending.buffer) {
                // no buffer to release: a get_release for this commit has
                // nothing to wait on
                fireReleaseCb(s.pending.releaseCb);

                if (cache) {
                    cache->valid = true;
                    cache->hasContent = false;
                    cache->width = cache->height = 0;
                    cache->pixels.clear();
                } else {
                    s.hasContent = false;
                    s.width = s.height = 0;
                }
            } else if (wl_shm_buffer* shm = wl_shm_buffer_get(s.pending.buffer)) {
                RectI dmg = s.pending.damage;
                bool all = s.pending.damageAll || dmg.empty();

                clipRect(dmg, wl_shm_buffer_get_width(shm), wl_shm_buffer_get_height(shm));

                if (cache) {
                    copyShmBufferTo(*shm, cache->width, cache->height, cache->pixels, nullptr);
                    cache->hasContent = cache->width > 0;
                    cache->valid = true;
                } else {
                    copyShmBuffer(s, shm, all ? nullptr : &dmg);

                    if (all) {
                        s.damageAll = true;
                    } else {
                        unionRect(s.damage, dmg);
                    }
                }

                wl_buffer_send_release(s.pending.buffer);
                // shm is copied out at commit, so its release is immediate
                fireReleaseCb(s.pending.releaseCb);

                if (!cache) {
                    releaseHeldDmabuf(s);
                }
            } else if (SpbBox* spb = spbFromRes(s.pending.buffer)) {
                if (cache) {
                    cache->width = cache->height = 1;
                    cache->pixels.clear();
                    cache->pixels.append((const u8*)&spb->argb, 4);
                    cache->hasContent = true;
                    cache->valid = true;
                } else {
                    s.width = s.height = 1;
                    s.pixels.clear();
                    s.pixels.append((const u8*)&spb->argb, 4);
                    s.hasContent = true;
                    s.dirty = true;
                    s.damageAll = true;
                }

                wl_buffer_send_release(s.pending.buffer);
                fireReleaseCb(s.pending.releaseCb);

                if (!cache) {
                    releaseHeldDmabuf(s);
                }
            } else if (DmabufBuffer* db = dmabufFromRes(s.pending.buffer)) {
                if (cache) {
                    // the cached buffer is released later; the release
                    // callback rides with it
                    dropReleaseCb(cache->releaseCb);
                    cache->releaseCb = s.pending.releaseCb;
                    s.pending.releaseCb = nullptr;
                    dmabufRef(db);
                    cache->dmabuf = db;
                    cache->dmabufRes = s.pending.buffer;
                    cache->dmabufDestroy.listener.notify = cachedDmabufDestroyed;
                    cache->dmabufDestroy.cache = cache;
                    wl_resource_add_destroy_listener(s.pending.buffer, &cache->dmabufDestroy.listener);
                    cache->dmabufDestroyArmed = true;
                    cache->width = db->width;
                    cache->height = db->height;
                    cache->hasContent = true;
                    cache->valid = true;
                    cache->pixels.clear();

                    if (s.pendAcqTl && s.pendRelTl) {
                        cache->acq = s.pendAcqTl;
                        cache->rel = s.pendRelTl;
                        cache->acquirePoint = s.pendAcqPt;
                        cache->releasePoint = s.pendRelPt;
                        s.pendAcqTl = s.pendRelTl = nullptr;
                    }
                } else {
                    DmabufUse* use = holdDmabuf(s, s.pending.buffer, db);

                    // the dmabuf releases when the frame that samples it
                    // retires; the release callback fires with it
                    if (use) {
                        use->releaseCb = s.pending.releaseCb;
                        s.pending.releaseCb = nullptr;
                    }

                    syncApplyPoints(s);
                    s.width = db->width;
                    s.height = db->height;
                    s.pixels.clear();
                    s.hasContent = true;
                    s.dirty = true;
                }
            } else {
                sysE << "imway: unknown buffer type"_sv << endL;
            }

            // any get_release that survived (unknown buffer) is dropped
            dropReleaseCb(s.pending.releaseCb);
            detachPendingBuffer(s);
            s.pending.newlyAttached = false;
            s.pending.attachX = s.pending.attachY = 0;
        }

        s.pending.damage = {};
        s.pending.damageAll = false;

        if (s.pending.scale > 0) {
            if (cache) {
                cache->scale = s.pending.scale;
                cache->scaleSet = true;
            } else if (s.pending.scale != s.bufferScale) {
                s.bufferScale = s.pending.scale;
                s.damageAll = true;
            }
        }

        s.pending.scale = 0;

        if (s.pending.transform >= 0) {
            if (cache) {
                cache->transform = s.pending.transform;
                cache->transformSet = true;
            } else if (s.pending.transform != s.bufferTransform) {
                s.bufferTransform = s.pending.transform;
                s.damageAll = true;
            }
        }

        s.pending.transform = -1;

        if (!cache) {
            constraintApplyPending(s);
        }

        if (cache) {
            if (s.pendSrcSet) {
                cache->vpSrcSet = true;
                cache->vpHasSrc = s.pendSw > 0;
                cache->sx = s.pendSx;
                cache->sy = s.pendSy;
                cache->sw = s.pendSw;
                cache->sh = s.pendSh;
                s.pendSrcSet = false;
            }

            if (s.pendDstSet) {
                cache->vpDstSet = true;
                cache->vpHasDst = s.pendDw > 0;
                cache->dw = s.pendDw;
                cache->dh = s.pendDh;
                s.pendDstSet = false;
            }
        } else {
            viewportApplyPending(s);
        }

        bool targetHasSrc = cache && cache->vpSrcSet ? cache->vpHasSrc : s.vp.hasSrc;
        bool targetHasDst = cache && cache->vpDstSet ? cache->vpHasDst : s.vp.hasDst;
        bool targetHasContent = cache && cache->valid ? cache->hasContent : s.hasContent;

        ColorRepresentation targetRepresentation =
            cache && cache->representationChanged ?
            cache->representation : s.representation;
        if (s.pendRepresentationChanged) {
            targetRepresentation = s.pendRepresentation;
        }

        DmabufBuffer* targetDmabuf =
            cache && cache->valid ? cache->dmabuf : s.dmabuf;
        bool targetYuv = targetDmabuf &&
            (targetDmabuf->format == kFourccNv12 ||
             targetDmabuf->format == kFourccP010);
        bool incompatibleRepresentation = targetYuv ?
            targetRepresentation.coefficients ==
                WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY :
            targetRepresentation.chromaLocation ||
                (targetRepresentation.coefficients &&
                 targetRepresentation.coefficients !=
                    WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY);

        if (targetHasContent && incompatibleRepresentation) {
            wl_resource_post_error(
                s.representationRes,
                WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_PIXEL_FORMAT,
                targetYuv ?
                    "identity coefficients are incompatible with a YCbCr buffer" :
                    "YCbCr representation is incompatible with an RGB buffer");

            return;
        }

        if (targetHasSrc && targetHasContent) {
            int targetTransform = cache && cache->transformSet ? cache->transform : s.bufferTransform;
            int targetScale = cache && cache->scaleSet ? cache->scale : s.bufferScale;
            int targetW = cache && cache->valid ? cache->width : s.width;
            int targetH = cache && cache->valid ? cache->height : s.height;
            double sx = cache && cache->vpSrcSet ? cache->sx : s.vp.sx;
            double sy = cache && cache->vpSrcSet ? cache->sy : s.vp.sy;
            double sw = cache && cache->vpSrcSet ? cache->sw : s.vp.sw;
            double sh = cache && cache->vpSrcSet ? cache->sh : s.vp.sh;
            bool swapped = targetTransform == WL_OUTPUT_TRANSFORM_90 || targetTransform == WL_OUTPUT_TRANSFORM_270 ||
                           targetTransform == WL_OUTPUT_TRANSFORM_FLIPPED_90 || targetTransform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
            double contentW = (double)(swapped ? targetH : targetW) / targetScale;
            double contentH = (double)(swapped ? targetW : targetH) / targetScale;

            if (sx + sw > contentW || sy + sh > contentH) {
                if (s.vpRes) {
                    wl_resource_post_error(s.vpRes, WP_VIEWPORT_ERROR_OUT_OF_BUFFER, "source rectangle is outside the buffer");
                }

                return;
            }

            if (!targetHasDst && (sw != (int)sw || sh != (int)sh)) {
                if (s.vpRes) {
                    wl_resource_post_error(s.vpRes, WP_VIEWPORT_ERROR_BAD_SIZE, "fractional source size requires a destination size");
                }

                return;
            }
        }

        if (s.pendColorChanged) {
            if (cache) {
                cache->colorChanged = true;
                cache->color = *s.pendColor;
            } else {
                applyColorState(s, *s.pendColor);
            }

            s.pendColorChanged = false;
        }

        if (s.pendRepresentationChanged) {
            if (cache) {
                cache->representationChanged = true;
                cache->representation = s.pendRepresentation;
            } else {
                s.representation = s.pendRepresentation;
            }
            s.pendRepresentationChanged = false;
        }

        if (s.pendAlphaChanged) {
            if (cache) {
                cache->alphaChanged = true;
                cache->alphaMult = s.pendAlphaMult;
            } else {
                s.alphaMult = s.pendAlphaMult;
            }
            s.pendAlphaChanged = false;
        }

        if (s.pendContentChanged) {
            // a scanout/pacing hint only; no cache dance needed
            s.contentType = s.pendContentType;
            s.pendContentChanged = false;
        }

        if (s.pendTearingChanged) {
            s.tearingAsync = s.pendTearingAsync;
            s.pendTearingChanged = false;
        }

        if (s.pending.inputRegionChanged) {
            if (cache) {
                cache->inputChanged = true;
                cache->inputSet = s.pending.inputRegionSet;
                cache->inputRegion.clear();
                cache->inputRegion.append(s.pending.inputRegion.begin(), s.pending.inputRegion.length());
            } else {
                s.inputRegionSet = s.pending.inputRegionSet;
                s.inputRegion.clear();

                for (const RectI& pendingRect : s.pending.inputRegion) {
                    RectI r = pendingRect;

                    clipRect(r, s.viewW(), s.viewH());

                    if (!r.empty()) {
                        s.inputRegion.pushBack(r);
                    }
                }
            }

            s.pending.inputRegionChanged = false;
            s.pending.inputRegion.clear();
        }

        if (s.pending.opaqueRegionChanged) {
            if (cache) {
                cache->opaqueChanged = true;
                cache->opaqueSet = s.pending.opaqueRegionSet;
                cache->opaqueRegion.clear();
                cache->opaqueRegion.append(s.pending.opaqueRegion.begin(), s.pending.opaqueRegion.length());
            } else {
                s.opaqueRegionSet = s.pending.opaqueRegionSet;
                s.opaqueRegion.clear();
                s.opaqueRegion.append(s.pending.opaqueRegion.begin(), s.pending.opaqueRegion.length());
            }

            s.pending.opaqueRegionChanged = false;
            s.pending.opaqueRegion.clear();
        }

        if (cache) {
            for (wl_resource* cb : s.pending.frames) {
                cache->frames.pushBack(cb);
            }

            s.pending.frames.clear();

            return;
        }

        for (wl_resource* cb : s.pending.frames) {
            s.frameCbs.pushBack(cb);
        }

        s.pending.frames.clear();

        applyChildrenCaches(s);

        if (s.xdg) {
            xdgHandleCommit(s);
        }
    }

    void surfaceSetBufferTransform(wl_client*, wl_resource* res, i32 transform) {
        if (transform < WL_OUTPUT_TRANSFORM_NORMAL || transform > WL_OUTPUT_TRANSFORM_FLIPPED_270) {
            wl_resource_post_error(res, WL_SURFACE_ERROR_INVALID_TRANSFORM, "unknown buffer transform");

            return;
        }

        surfaceFrom(res)->pending.transform = transform;
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

    void surfaceOffset(wl_client*, wl_resource* res, i32 x, i32 y) {
        SurfaceImpl& s = *surfaceFrom(res);

        // double-buffered: the offset moves the buffer relative to the surface
        // and applies with the next commit alongside the pending attach
        s.pending.attachX = x;
        s.pending.attachY = y;
    }

    void surfaceGetRelease(wl_client* client, wl_resource* res, u32 id) {
        SurfaceImpl& s = *surfaceFrom(res);

        // double-buffered, one per commit: a second get_release before commit
        // replaces the first (its buffer is the same pending buffer)
        dropReleaseCb(s.pending.releaseCb);
        s.pending.releaseCb = wl_resource_create(client, &wl_callback_interface, 1, id);

        if (!s.pending.releaseCb) {
            wl_client_post_no_memory(client);
        }
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
        .get_release = surfaceGetRelease,
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
        // an uncommitted get_release callback never gets a buffer
        dropReleaseCb(s->pending.releaseCb);

        for (wl_resource* cb : s->pending.frames) {
            wl_resource_set_user_data(cb, nullptr);
        }

        for (wl_resource* cb : s->frameCbs) {
            wl_resource_set_user_data(cb, nullptr);
        }

        // sync-subsurface commits park frame callbacks in sub->cache.frames;
        // they too point at this SurfaceImpl and outlive it until the
        // wl_subsurface dies
        if (s->sub) {
            for (wl_resource* cb : impl(s->sub).cache.frames) {
                wl_resource_set_user_data(cb, nullptr);
            }
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

        forEach<Subsurface>(s->stackBelow, [](Subsurface& c) {
            c.parent = nullptr;
        });

        forEach<Subsurface>(s->stackAbove, [](Subsurface& c) {
            c.parent = nullptr;
        });

        // the list heads die with this surface: detach them so the orphaned
        // children keep a valid ring among themselves and their own later
        // unlink()s touch no freed memory
        s->stackBelow.clear();
        s->stackAbove.clear();

        forEach<Popup>(srv->scene->popups, [&](Popup& p) {
            if (p.parent == s) {
                p.parent = nullptr;

                if (p.mapped) {
                    dismissPopupTree((PopupImpl&)p);
                }
            }
        });

        forEach<IdleInhibitor>(srv->idleInhibitors, [&](IdleInhibitor& inhibitor) {
            if (inhibitor.surface == s) {
                inhibitor.surface = nullptr;
            }
        });

        forEach<ActivationTokenRequest>(srv->activationTokenRequests, [&](ActivationTokenRequest& request) {
            if (request.surface == s) {
                request.surface = nullptr;
            }
        });

        srv->seat.surfaceGone(s);

        releaseHeldDmabuf(*s);
        viewportSurfaceGone(*s);
        alphaModSurfaceGone(*s);
        contentTypeSurfaceGone(*s);
        tearingSurfaceGone(*s);

        if (srv->scene->imePopup == (Surface*)s) {
            srv->scene->imePopup = nullptr;
        }

        if (srv->seat.inputMethod && srv->seat.inputMethod->popupSurface == s) {
            srv->seat.inputMethod->popupSurface = nullptr;
        }

        fifoSurfaceGone(*s);
        fracSurfaceGone(*s);
        constraintSurfaceGone(*s);
        kbInhibitSurfaceGone(*s);
        syncSurfaceGone(*s);

        if (s->colorRes) {
            wl_resource_set_user_data(s->colorRes, nullptr);
            s->colorRes = nullptr;
        }


        if (s->representationRes) {
            wl_resource_set_user_data(s->representationRes, nullptr);
            s->representationRes = nullptr;
        }

        if (s->frame) {
            FrameResource* frame = s->frame;

            s->texture = nullptr;
            s->frame = nullptr;
            frameUnref(frame);
        }

        if (s->pendColor) {
            srv->alloc->release(s->pendColor);
        }

        srv->scene->needsFrame = true;
        ((SceneNode*)s)->unlink();
        srv->alloc->release(s);
    }

    void regionDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void regionAdd(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        if (w <= 0 || h <= 0) {
            return;
        }

        constexpr i64 maxI32 = 0x7fffffff;
        i64 x2 = (i64)x + w, y2 = (i64)y + h;

        if (x2 > maxI32) {
            x2 = maxI32;
        }

        if (y2 > maxI32) {
            y2 = maxI32;
        }

        if (x2 > x && y2 > y) {
            ((RegionBox*)wl_resource_get_user_data(res))->rects.pushBack({x, y, (i32)(x2 - x), (i32)(y2 - y)});
        }
    }

    void regionSubtract(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        auto& v = ((RegionBox*)wl_resource_get_user_data(res))->rects;

        if (w <= 0 || h <= 0) {
            return;
        }

        i64 sx1 = x, sy1 = y, sx2 = (i64)x + w, sy2 = (i64)y + h;
        Vector<RectI> out;

        for (size_t i = 0; i < v.length(); i++) {
            const RectI& r = v[i];
            i64 rx1 = r.x, ry1 = r.y, rx2 = (i64)r.x + r.w, ry2 = (i64)r.y + r.h;
            i64 ix1 = rx1 > sx1 ? rx1 : sx1;
            i64 iy1 = ry1 > sy1 ? ry1 : sy1;
            i64 ix2 = rx2 < sx2 ? rx2 : sx2;
            i64 iy2 = ry2 < sy2 ? ry2 : sy2;

            if (ix1 >= ix2 || iy1 >= iy2) {
                out.pushBack(r);

                continue;
            }

            if (ry1 < iy1) {
                out.pushBack({r.x, r.y, r.w, (i32)(iy1 - ry1)});
            }

            if (iy2 < ry2) {
                out.pushBack({r.x, (i32)iy2, r.w, (i32)(ry2 - iy2)});
            }

            if (rx1 < ix1) {
                out.pushBack({r.x, (i32)iy1, (i32)(ix1 - rx1), (i32)(iy2 - iy1)});
            }

            if (ix2 < rx2) {
                out.pushBack({(i32)ix2, (i32)iy1, (i32)(rx2 - ix2), (i32)(iy2 - iy1)});
            }
        }

        v.xchg(out);
    }

    const struct wl_region_interface regionImpl = {
        .destroy = regionDestroy,
        .add = regionAdd,
        .subtract = regionSubtract,
    };

    void regionResourceDestroyed(wl_resource* res) {
        auto* box = (RegionBox*)wl_resource_get_user_data(res);

        box->srv->alloc->release(box);
    }

    void compositorCreateSurface(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* sres = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(res), id);

        if (!sres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* s = srv->alloc->make<SurfaceImpl>();

        s->srv = srv;
        s->res = sres;
        srv->scene->surfaces.pushBack((SceneNode*)s);
        wl_resource_set_implementation(sres, &surfaceImpl, s, surfaceResourceDestroyed);

        // v6: we present every buffer at scale 1, untransformed; tell the
        // client so it can pick a matching buffer up front
        if (wl_resource_get_version(sres) >= WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION) {
            wl_surface_send_preferred_buffer_scale(sres, 1);
            wl_surface_send_preferred_buffer_transform(sres, WL_OUTPUT_TRANSFORM_NORMAL);
        }
    }

    void compositorCreateRegion(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* rres = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(res), id);

        if (!rres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* box = srv->alloc->make<RegionBox>();

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
        // unlink unconditionally: after the parent died the node still sits
        // in the orphan ring of its former siblings
        sub.unlink();
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

    bool subsurfaceRestack(SubsurfaceImpl& sub, Surface* refSurface, bool above) {
        Surface* parent = sub.parent;

        if (!parent) {
            return false;
        }

        if (refSurface == parent) {
            sub.unlink();

            if (above) {
                // bottom of the above-pile: directly on top of the parent
                parent->stackAbove.pushFront(&sub);
            } else {
                // top of the below-pile: directly under the parent
                parent->stackBelow.pushBack(&sub);
            }

            return true;
        }

        Subsurface* ref = refSurface->sub;

        if (!ref || ref->parent != parent || ref == &sub) {
            return false;
        }

        // ref->parent == parent guarantees ref sits in one of the parent's
        // piles, so the relative insert needs no search at all
        sub.unlink();

        if (above) {
            IntrusiveList::insertAfter(ref, &sub);
        } else {
            IntrusiveList::insertBefore(ref, &sub);
        }

        return true;
    }

    void subsurfacePlaceAbove(wl_client*, wl_resource* res, wl_resource* sibling) {
        SubsurfaceImpl* sub = subFrom(res);

        if (sub && sibling && !subsurfaceRestack(*sub, surfaceFrom(sibling), true)) {
            wl_resource_post_error(res, WL_SUBSURFACE_ERROR_BAD_SURFACE, "surface is not a sibling or parent");
        }
    }

    void subsurfacePlaceBelow(wl_client*, wl_resource* res, wl_resource* sibling) {
        SubsurfaceImpl* sub = subFrom(res);

        if (sub && sibling && !subsurfaceRestack(*sub, surfaceFrom(sibling), false)) {
            wl_resource_post_error(res, WL_SUBSURFACE_ERROR_BAD_SURFACE, "surface is not a sibling or parent");
        }
    }

    void subsurfaceSetSync(wl_client*, wl_resource* res) {
        if (SubsurfaceImpl* sub = subFrom(res)) {
            sub->sync = true;
        }
    }

    void subsurfaceSetDesync(wl_client*, wl_resource* res) {
        SubsurfaceImpl* sub = subFrom(res);

        if (!sub || !sub->surface) {
            return;
        }

        sub->sync = false;

        if (!sub->effectiveSync()) {
            applySubsurfaceCache(*sub);
            sub->srv->scene->needsFrame = true;
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

        releaseCachedDmabuf(sub->srv, sub->cache);

        if (sub->surface) {
            sub->surface->sub = nullptr;
        }

        sub->srv->alloc->release(sub);
    }

    void subcompositorDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void subcompositorGetSubsurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes, wl_resource* parentRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        SurfaceImpl* surface = surfaceFrom(surfaceRes);
        SurfaceImpl* parent = surfaceFrom(parentRes);

        if (!surface) {
            wl_resource_post_error(res, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "invalid child surface");

            return;
        }

        if (!parent) {
            wl_resource_post_error(res, WL_SUBCOMPOSITOR_ERROR_BAD_PARENT, "invalid parent surface");

            return;
        }

        if (surface->role != SurfaceRole::none || surface->xdg || surface->sub) {
            wl_resource_post_error(res, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "surface already has a role");

            return;
        }

        Surface* ancestor = parent;
        size_t remaining = srv->scene->surfaces.length() + 1;

        while (ancestor && remaining-- > 0) {
            if (ancestor == surface) {
                wl_resource_post_error(res, WL_SUBCOMPOSITOR_ERROR_BAD_PARENT, "subsurface hierarchy would contain a cycle");

                return;
            }

            ancestor = ancestor->sub ? ancestor->sub->parent : nullptr;
        }

        if (ancestor) {
            wl_resource_post_error(res, WL_SUBCOMPOSITOR_ERROR_BAD_PARENT, "invalid subsurface hierarchy");

            return;
        }

        wl_resource* sres = wl_resource_create(client, &wl_subsurface_interface, wl_resource_get_version(res), id);

        if (!sres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* sub = srv->alloc->make<SubsurfaceImpl>();

        sub->srv = srv;
        sub->surface = surface;
        sub->parent = parent;
        sub->res = sres;
        surface->role = SurfaceRole::subsurface;
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
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (t->decoRes) {
            wl_resource_post_error(t->decoRes, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ORPHANED,
                                   "xdg_toplevel destroyed before its decoration object");

            return;
        }

        wl_resource_destroy(res);
    }

    void toplevelSetParent(wl_client*, wl_resource*, wl_resource*) {
    }

    void toplevelSetTitle(wl_client*, wl_resource* res, const char* title) {
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(res);

        t->title.reset();
        t->title << title;
        t->srv->scene->needsFrame = true;
    }

    void toplevelSetAppId(wl_client*, wl_resource* res, const char* appId) {
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(res);

        t->appId.reset();
        t->appId << appId;

        // resolve right here: a client icon set via xdg-toplevel-icon wins
        if (!t->iconFromClient && t->srv->icons) {
            t->icon = t->srv->icons->forAppId(sv(t->appId));
            t->srv->scene->needsFrame = true;
        }
    }

    bool validToplevelGrab(ToplevelImpl& toplevel, wl_client* client, wl_resource* seatRes, u32 serial) {
        auto* seat = (SeatState*)wl_resource_get_user_data(seatRes);

        return seat == &toplevel.srv->seat && seat->buttonsDown > 0 &&
               seat->pointerGrabClient == client && seat->pointerGrabSerial == serial &&
               seat->pointerGrabOrigin && seat->pointerGrabOrigin->rootToplevel() == &toplevel;
    }

    void toplevelShowWindowMenu(wl_client*, wl_resource*, wl_resource*, u32, i32, i32) {
        // imway has no per-window context menu; the request is intentionally
        // ignored, as permitted by xdg-shell.
    }

    void toplevelMove(wl_client* client, wl_resource* res, wl_resource* seatRes, u32 serial) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (ti && validToplevelGrab(*ti, client, seatRes, serial)) {
            ti->moveRequested = true;
            ti->srv->scene->needsFrame = true;
        }
    }

    void toplevelResize(wl_client* client, wl_resource* res, wl_resource* seatRes, u32 serial, u32 edges) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (!xdg_toplevel_resize_edge_is_valid(edges, wl_resource_get_version(res)) ||
            edges == XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
            wl_resource_post_error(res, XDG_TOPLEVEL_ERROR_INVALID_RESIZE_EDGE, "invalid resize edge");

            return;
        }

        if (ti && validToplevelGrab(*ti, client, seatRes, serial)) {
            ti->resizeEdges = edges;
            ti->srv->scene->needsFrame = true;
        }
    }

    void toplevelSetMaxSize(wl_client*, wl_resource* res, i32 w, i32 h) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (w < 0 || h < 0) {
            wl_resource_post_error(res, XDG_TOPLEVEL_ERROR_INVALID_SIZE, "maximum size must not be negative");

            return;
        }

        ti->pendingMaxW = w;
        ti->pendingMaxH = h;
        ti->pendingMaxSet = true;
    }

    void toplevelSetMinSize(wl_client*, wl_resource* res, i32 w, i32 h) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (w < 0 || h < 0) {
            wl_resource_post_error(res, XDG_TOPLEVEL_ERROR_INVALID_SIZE, "minimum size must not be negative");

            return;
        }

        ti->pendingMinW = w;
        ti->pendingMinH = h;
        ti->pendingMinSet = true;
    }

    void toplevelSetMaximized(wl_client*, wl_resource* res) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (ti && !ti->maximized) {
            ti->maximized = true;
            ti->minimized = false;
            ti->restoreRequested = false;
            ti->srv->scene->needsFrame = true;
        }
    }

    void toplevelUnsetMaximized(wl_client*, wl_resource* res) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (ti && ti->maximized) {
            ti->maximized = false;
            ti->restoreRequested = ti->restoreW > 0 && ti->restoreH > 0;
            ti->srv->scene->needsFrame = true;
        }
    }

    void toplevelSetFullscreen(wl_client*, wl_resource* res, wl_resource*) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (!ti || ti->fullscreen) {
            return;
        }

        ti->fullscreen = true;
        // a floating window that sized itself was only ever configured 0x0;
        // park the real geometry so unset_fullscreen can restore it — a 0x0
        // restore configure leaves the client at the fullscreen size
        ti->prevW = ti->cfgW ? ti->cfgW : (ti->surface ? ti->surface->geomW() : 0);
        ti->prevH = ti->cfgH ? ti->cfgH : (ti->surface ? ti->surface->geomH() : 0);
        xdgToplevelConfigureSize(*ti, ti->srv->scene->outW, ti->srv->scene->outH);
        ti->srv->scene->needsFrame = true;
    }

    void toplevelUnsetFullscreen(wl_client*, wl_resource* res) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (!ti || !ti->fullscreen) {
            return;
        }

        ti->fullscreen = false;
        xdgToplevelConfigureSize(*ti, ti->prevW, ti->prevH);
        ti->srv->scene->needsFrame = true;
    }

    void toplevelSetMinimized(wl_client*, wl_resource* res) {
        auto* ti = (ToplevelImpl*)wl_resource_get_user_data(res);

        if (ti) {
            ti->minimized = true;

            if (ti->srv->scene->focusedToplevel == ti) {
                ti->srv->scene->focusedToplevel = nullptr;
            }

            ti->srv->scene->needsFrame = true;
        }
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

        // the renderer stashes this between buildUi and the frame event; the client
        // can destroy the toplevel in that window (KMS: render -> page flip)
        if (srv->scene->focusedToplevel == t) {
            srv->scene->focusedToplevel = nullptr;
        }

        if (t->xdg) {
            t->xdg->toplevel = nullptr;
        }

        if (t->surface) {
            t->surface->toplevel = nullptr;
        }

        if (t->decoRes) {
            wl_resource_set_user_data(t->decoRes, nullptr);
            t->decoRes = nullptr;
        }

        if (t->dialogRes) {
            wl_resource_set_user_data(t->dialogRes, nullptr);
            t->dialogRes = nullptr;
        }

        if (t->ownIcon && srv->iconPool) {
            srv->iconPool->release(t->ownIcon);
            t->ownIcon = nullptr;
        }

        if (t->pendingOwnIcon && srv->iconPool) {
            srv->iconPool->release(t->pendingOwnIcon);
            t->pendingOwnIcon = nullptr;
        }

        t->unlink();
        sysO << "imway: toplevel "_sv << sv(t->title) << " destroyed"_sv << endL;
        srv->scene->needsFrame = true;
        srv->alloc->release(t);
    }

    void xdgSurfaceDestroy(wl_client*, wl_resource* res) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);

        if (xs && (xs->toplevel || xs->popup)) {
            wl_resource_post_error(res, XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT, "xdg_surface destroyed before its role object");

            return;
        }

        wl_resource_destroy(res);
    }

    void sendConfigureBounds(ToplevelImpl& t) {
        // the work area the client should fit into (v4); sent before the
        // configure so a fresh toplevel can pick its initial size
        if (wl_resource_get_version(t.res) >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION) {
            xdg_toplevel_send_configure_bounds(t.res, t.srv->scene->workW, t.srv->scene->workH);
        }
    }

    void sendConfigure(XdgSurface& xs) {
        if (xs.toplevel) {
            wl_array states;

            wl_array_init(&states);
            sendConfigureBounds(*xs.toplevel);
            xdg_toplevel_send_configure(xs.toplevel->res, 0, 0, &states);
            wl_array_release(&states);
        } else if (xs.popup) {
            PopupImpl& p = *xs.popup;

            xdg_popup_send_configure(p.res, p.x, p.y, p.w, p.h);
        }

        sendXdgSurfaceConfigure(xs);
        xs.initialConfigureSent = true;
    }

    void xdgSurfaceGetToplevel(wl_client* client, wl_resource* res, u32 id) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);

        if (xs->toplevel || xs->popup || !xs->surface || xs->surface->role != SurfaceRole::none) {
            wl_resource_post_error(res, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED, "xdg_surface already has a role object");

            return;
        }

        wl_resource* tres = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(res), id);

        if (!tres) {
            wl_client_post_no_memory(client);

            return;
        }

        WaylandImpl* srv = xs->srv;
        auto* t = srv->alloc->make<ToplevelImpl>();

        t->srv = srv;
        t->res = tres;
        t->xdg = xs;
        t->surface = xs->surface;
        t->id = srv->nextToplevelId++;
        t->title << "(untitled)"_sv;
        xs->toplevel = t;
        xs->surface->role = SurfaceRole::xdgToplevel;

        if (xs->surface) {
            xs->surface->toplevel = t;
        }

        srv->scene->toplevels.pushBack(t);
        wl_resource_set_implementation(tres, &toplevelImpl, t, toplevelResourceDestroyed);

        // static window-management capabilities (v5): advertise what the ui
        // actually offers so clients can hide unavailable menu entries. No
        // window_menu — we have no client-driven window menu.
        if (wl_resource_get_version(tres) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
            wl_array caps;

            wl_array_init(&caps);
            *(u32*)wl_array_add(&caps, sizeof(u32)) = XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE;
            *(u32*)wl_array_add(&caps, sizeof(u32)) = XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN;
            *(u32*)wl_array_add(&caps, sizeof(u32)) = XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
            xdg_toplevel_send_wm_capabilities(tres, &caps);
            wl_array_release(&caps);
        }
    }

    void xdgSurfaceGetPopup(wl_client* client, wl_resource* res, u32 id, wl_resource* parentRes, wl_resource* positionerRes);

    void xdgSurfaceSetWindowGeometry(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);

        if (w <= 0 || h <= 0) {
            wl_resource_post_error(res, XDG_SURFACE_ERROR_INVALID_SIZE, "window geometry must have a positive size");

            return;
        }

        xs->pendGeom = {x, y, w, h};
        xs->pendGeomSet = true;
    }

    void xdgSurfaceAckConfigure(wl_client*, wl_resource* res, u32 serial) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);
        size_t found = xs->configureSerials.length();

        for (size_t i = 0; i < xs->configureSerials.length(); i++) {
            if (xs->configureSerials[i] == serial) {
                found = i;

                break;
            }
        }

        if (found == xs->configureSerials.length()) {
            wl_resource_post_error(res, XDG_SURFACE_ERROR_INVALID_SERIAL, "unknown configure serial %u", serial);

            return;
        }

        size_t keep = 0;

        for (size_t i = found + 1; i < xs->configureSerials.length(); i++) {
            xs->configureSerials.mut(keep++) = xs->configureSerials[i];
        }

        while (xs->configureSerials.length() > keep) {
            xs->configureSerials.popBack();
        }

        xs->acked = true;
        xs->ackedSerial = serial;
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

        xs->srv->alloc->release(xs);
    }

    Positioner* positionerFrom(wl_resource* res) {
        return (Positioner*)wl_resource_get_user_data(res);
    }

    void positionerDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void positionerSetSize(wl_client*, wl_resource* res, i32 w, i32 h) {
        if (w <= 0 || h <= 0) {
            wl_resource_post_error(res, XDG_POSITIONER_ERROR_INVALID_INPUT, "positioner size must be positive");

            return;
        }

        Positioner* p = positionerFrom(res);

        p->w = w;
        p->h = h;
    }

    void positionerSetAnchorRect(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        if (w <= 0 || h <= 0) {
            wl_resource_post_error(res, XDG_POSITIONER_ERROR_INVALID_INPUT, "anchor rectangle must be positive");

            return;
        }

        Positioner* p = positionerFrom(res);

        p->ax = x;
        p->ay = y;
        p->aw = w;
        p->ah = h;
    }

    void positionerSetAnchor(wl_client*, wl_resource* res, u32 a) {
        if (!xdg_positioner_anchor_is_valid(a, wl_resource_get_version(res))) {
            wl_resource_post_error(res, XDG_POSITIONER_ERROR_INVALID_INPUT, "unknown anchor");

            return;
        }

        positionerFrom(res)->anchor = a;
    }

    void positionerSetGravity(wl_client*, wl_resource* res, u32 g) {
        if (!xdg_positioner_gravity_is_valid(g, wl_resource_get_version(res))) {
            wl_resource_post_error(res, XDG_POSITIONER_ERROR_INVALID_INPUT, "unknown gravity");

            return;
        }

        positionerFrom(res)->gravity = g;
    }

    void positionerSetConstraintAdjustment(wl_client*, wl_resource* res, u32 constraints) {
        if (!xdg_positioner_constraint_adjustment_is_valid(constraints, wl_resource_get_version(res))) {
            wl_resource_post_error(res, XDG_POSITIONER_ERROR_INVALID_INPUT, "unknown constraint adjustment");

            return;
        }

        positionerFrom(res)->constraints = constraints;
    }

    void positionerSetOffset(wl_client*, wl_resource* res, i32 x, i32 y) {
        Positioner* p = positionerFrom(res);

        p->dx = x;
        p->dy = y;
    }

    void positionerSetReactive(wl_client*, wl_resource* res) {
        positionerFrom(res)->reactive = true;
    }

    void positionerSetParentSize(wl_client*, wl_resource* res, i32 w, i32 h) {
        if (w <= 0 || h <= 0) {
            wl_resource_post_error(res, XDG_POSITIONER_ERROR_INVALID_INPUT, "parent size must be positive");

            return;
        }

        Positioner* p = positionerFrom(res);

        p->parentW = w;
        p->parentH = h;
    }

    void positionerSetParentConfigure(wl_client*, wl_resource* res, u32 serial) {
        positionerFrom(res)->parentConfigure = serial;
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

        p->srv->alloc->release(p);
    }

    int clampPosition(i64 v) {
        constexpr i64 min = -2147483647LL - 1;
        constexpr i64 max = 2147483647LL;

        return (int)(v < min ? min : v > max ? max : v);
    }

    void placePopup(const PopupImpl& popup, const Positioner& positioner,
                    int& x, int& y, int& w, int& h) {
        int minX = 0, minY = 0;
        int maxX = popup.srv->scene->outW, maxY = popup.srv->scene->outH;

        if (popup.parent) {
            i64 parentX = (i64)popup.parent->imgX + popup.parent->geomX();
            i64 parentY = (i64)popup.parent->imgY + popup.parent->geomY();

            minX = clampPosition(-parentX);
            minY = clampPosition(-parentY);
            maxX = clampPosition((i64)popup.srv->scene->outW - parentX);
            maxY = clampPosition((i64)popup.srv->scene->outH - parentY);
        }

        positioner.place(x, y, w, h, minX, minY, maxX, maxY);
    }

    void configurePopupPosition(PopupImpl& popup, const Positioner& positioner, bool repositioned, u32 token) {
        placePopup(popup, positioner, popup.pendingX, popup.pendingY, popup.pendingW, popup.pendingH);
        popup.positioner = positioner;
        popup.positionPending = true;

        if (repositioned) {
            xdg_popup_send_repositioned(popup.res, token);
        }

        xdg_popup_send_configure(popup.res, popup.pendingX, popup.pendingY, popup.pendingW, popup.pendingH);
        popup.positionSerial = sendXdgSurfaceConfigure(*popup.xdg);
    }

    void popupDestroy(wl_client*, wl_resource* res) {
        auto* popup = (PopupImpl*)wl_resource_get_user_data(res);

        for (Popup* child : each<Popup>(popup->srv->scene->popups)) {
            if (child->parent == popup->surface) {
                wl_resource_post_error(popup->xdg->wmBaseRes, XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP,
                                       "popup has a live child popup");

                return;
            }
        }

        wl_resource_destroy(res);
    }

    void popupGrab(wl_client* client, wl_resource* res, wl_resource* seatRes, u32 serial) {
        auto* p = (PopupImpl*)wl_resource_get_user_data(res);
        auto* seat = (SeatState*)wl_resource_get_user_data(seatRes);

        auto* parentSurface = (SurfaceImpl*)p->parent;
        PopupImpl* parentPopup = parentSurface && parentSurface->xdg ? parentSurface->xdg->popup : nullptr;

        if (p->mapped || !seat || seat != &p->srv->seat || (parentPopup && !parentPopup->grab)) {
            wl_resource_post_error(res, XDG_POPUP_ERROR_INVALID_GRAB, "popup grab serial is not an active implicit grab");

            return;
        }

        bool pointerOk = seat->pointerGrabClient == client && seat->pointerGrabSerial == serial && seat->buttonsDown > 0;
        bool keyOk = seat->keyGrabClient == client &&
                     seat->keyGrabSerial == serial &&
                     seat->keyGrabGeneration == seat->focusGeneration;

        // keyboard-opened menus (Menu key, Shift+F10) grab with a key-press
        // serial; a stale serial is not an error per xdg-shell — dismiss the
        // popup instead of killing the client
        if (!pointerOk && !keyOk) {
            xdg_popup_send_popup_done(res);

            return;
        }

        p->grab = true;
    }

    void popupReposition(wl_client*, wl_resource* res, wl_resource* positioner, u32 token) {
        auto* p = (PopupImpl*)wl_resource_get_user_data(res);
        Positioner* pos = positionerFrom(positioner);

        if (!pos || pos->w <= 0 || pos->h <= 0 || pos->aw <= 0 || pos->ah <= 0) {
            wl_resource_post_error(p->xdg->wmBaseRes, XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                                   "positioner needs size and anchor rectangle");

            return;
        }

        configurePopupPosition(*p, *pos, true, token);
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

        p->unlink();
        srv->scene->needsFrame = true;
        srv->alloc->release(p);
    }

    void xdgSurfaceGetPopup(wl_client* client, wl_resource* res, u32 id, wl_resource* parentRes, wl_resource* positionerRes) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);
        Positioner* pos = positionerFrom(positionerRes);

        if (xs->toplevel || xs->popup || !xs->surface || xs->surface->role != SurfaceRole::none) {
            wl_resource_post_error(res, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED, "xdg_surface already has a role object");

            return;
        }

        auto* parentXs = parentRes ? (XdgSurface*)wl_resource_get_user_data(parentRes) : nullptr;

        if (parentRes && (!parentXs || (!parentXs->toplevel && !parentXs->popup) || !parentXs->surface)) {
            wl_resource_post_error(xs->wmBaseRes, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT, "popup parent has no constructed role");

            return;
        }

        if (!pos || pos->w <= 0 || pos->h <= 0 || pos->aw <= 0 || pos->ah <= 0) {
            wl_resource_post_error(xs->wmBaseRes, XDG_WM_BASE_ERROR_INVALID_POSITIONER, "positioner needs size and anchor rectangle");

            return;
        }

        wl_resource* pres = wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        WaylandImpl* srv = xs->srv;
        auto* p = srv->alloc->make<PopupImpl>();

        p->srv = srv;
        p->res = pres;
        p->xdg = xs;
        p->surface = xs->surface;
        p->parent = parentXs ? parentXs->surface : nullptr;

        p->positioner = *pos;
        placePopup(*p, *pos, p->x, p->y, p->w, p->h);
        xs->popup = p;
        xs->surface->role = SurfaceRole::xdgPopup;
        srv->scene->popups.pushBack(p);
        wl_resource_set_implementation(pres, &popupImpl, p, popupResourceDestroyed);
    }

    void wmBaseDestroy(wl_client*, wl_resource* res) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);

        for (Surface* surface : each<Surface, SceneNode>(srv->scene->surfaces)) {
            auto* impl = (SurfaceImpl*)surface;

            if (impl->xdg && impl->xdg->wmBaseRes == res) {
                wl_resource_post_error(res, XDG_WM_BASE_ERROR_DEFUNCT_SURFACES,
                                       "xdg_wm_base still owns xdg_surface objects");

                return;
            }
        }

        wl_resource_destroy(res);
    }

    void wmBaseCreatePositioner(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* pres = wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        Positioner* p = srv->alloc->make<Positioner>();

        p->srv = srv;
        wl_resource_set_implementation(pres, &positionerImpl, p, positionerResourceDestroyed);
    }

    void wmBaseGetXdgSurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        auto* surface = surfaceFrom(surfaceRes);
        if (surface->xdg || surface->role != SurfaceRole::none) {
            wl_resource_post_error(res, XDG_WM_BASE_ERROR_ROLE, "wl_surface already has a role or xdg_surface");

            return;
        }

        if (surface->hasContent || surface->pending.buffer) {
            wl_resource_post_error(res, XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE, "wl_surface has a buffer before xdg_surface creation");

            return;
        }

        wl_resource* xres = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(res), id);

        if (!xres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* xs = srv->alloc->make<XdgSurface>();

        xs->srv = srv;
        xs->res = xres;
        xs->wmBaseRes = res;
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
        // Resource teardown on disconnect is not ordered by role hierarchy.
        // Keep a half-torn-down toplevel inert until its own destroy callback
        // removes it from the scene.
        if (!t.res || !t.xdg) {
            return;
        }

        wl_array states;

        wl_array_init(&states);

        if (t.activated) {
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_ACTIVATED;
        }

        if (t.fullscreen) {
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_FULLSCREEN;
        }

        if (t.maximized) {
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_MAXIMIZED;
        }

        // a minimized window is not visible: tell the client (v6) so it can
        // throttle or stop rendering
        if (t.minimized &&
            wl_resource_get_version(t.res) >= XDG_TOPLEVEL_STATE_SUSPENDED_SINCE_VERSION) {
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_SUSPENDED;
        }

        // csd windows are "tiled": the toolkits (GTK) then drop their drop
        // shadows, invisible resize margins and rounded corners, which we
        // would otherwise have to crop via window geometry; ssd clients must
        // NOT get it — a tiled window has to fill the size exactly, which
        // disables cell-snapped resizing in terminals (foot resize-by-cells)
        if ((t.csd || t.docked) && wl_resource_get_version(t.res) >= XDG_TOPLEVEL_STATE_TILED_LEFT_SINCE_VERSION) {
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_TILED_LEFT;
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_TILED_RIGHT;
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_TILED_TOP;
            *(u32*)wl_array_add(&states, sizeof(u32)) = XDG_TOPLEVEL_STATE_TILED_BOTTOM;
        }

        sendConfigureBounds(t);
        xdg_toplevel_send_configure(t.res, w, h, &states);
        wl_array_release(&states);

        u32 serial = sendXdgSurfaceConfigure(*t.xdg);
        t.cfgSerial = serial;
        t.cfgW = w;
        t.cfgH = h;
        t.cfgDocked = t.docked;
        t.cfgMaximized = t.maximized;
        sysO << "imway: configure "_sv << sv(t.title) << " -> "_sv << w << "x"_sv << h << endL;
    }

    void xdgToplevelReconfigure(ToplevelImpl& t) {
        xdgToplevelConfigureSize(t, t.cfgW, t.cfgH);
    }

    void xdgHandleCommit(SurfaceImpl& s) {
        XdgSurface* xs = s.xdg;

        if (!xs) {
            return;
        }

        xs->committedAckSerial = xs->ackedSerial;

        if (xs->toplevel && xs->toplevel->pendingIconSet) {
            ToplevelImpl& toplevel = *xs->toplevel;

            if (toplevel.ownIcon && toplevel.srv->iconPool) {
                toplevel.srv->iconPool->release(toplevel.ownIcon);
            }

            toplevel.ownIcon = toplevel.pendingOwnIcon;
            toplevel.icon = toplevel.pendingIcon;
            toplevel.iconFromClient = toplevel.pendingIconFromClient;
            toplevel.pendingOwnIcon = nullptr;
            toplevel.pendingIcon = nullptr;
            toplevel.pendingIconFromClient = false;
            toplevel.pendingIconSet = false;
            toplevel.srv->scene->needsFrame = true;
        }

        if (xs->popup && !xs->initialConfigureSent) {
            auto* parent = (SurfaceImpl*)xs->popup->parent;
            bool parentMapped = parent && parent->xdg &&
                                ((parent->xdg->toplevel && parent->xdg->toplevel->mapped) ||
                                 (parent->xdg->popup && parent->xdg->popup->mapped));

            if (!parentMapped) {
                wl_resource_post_error(xs->wmBaseRes, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
                                       "popup parent is missing or not mapped");

                return;
            }
        }

        if (xs->popup && xs->popup->positionPending &&
            (i32)(xs->committedAckSerial - xs->popup->positionSerial) >= 0) {
            PopupImpl& popup = *xs->popup;

            popup.x = popup.pendingX;
            popup.y = popup.pendingY;
            popup.w = popup.pendingW;
            popup.h = popup.pendingH;
            popup.positionPending = false;
            s.srv->scene->needsFrame = true;
        }

        if (xs->toplevel && (xs->toplevel->pendingMinSet || xs->toplevel->pendingMaxSet)) {
            ToplevelImpl& toplevel = *xs->toplevel;
            int minW = toplevel.pendingMinSet ? toplevel.pendingMinW : toplevel.minW;
            int minH = toplevel.pendingMinSet ? toplevel.pendingMinH : toplevel.minH;
            int maxW = toplevel.pendingMaxSet ? toplevel.pendingMaxW : toplevel.maxW;
            int maxH = toplevel.pendingMaxSet ? toplevel.pendingMaxH : toplevel.maxH;

            if ((minW && maxW && minW > maxW) || (minH && maxH && minH > maxH)) {
                wl_resource_post_error(toplevel.res, XDG_TOPLEVEL_ERROR_INVALID_SIZE,
                                       "minimum size exceeds maximum size");

                return;
            }

            toplevel.minW = minW;
            toplevel.minH = minH;
            toplevel.maxW = maxW;
            toplevel.maxH = maxH;
            toplevel.pendingMinSet = false;
            toplevel.pendingMaxSet = false;
        }

        if (xs->pendGeomSet) {
            s.geom = xs->pendGeom;
            s.hasGeom = true;
            xs->pendGeomSet = false;
            s.srv->scene->needsFrame = true;
        }

        if (!xs->initialConfigureSent) {
            sendConfigure(*xs);

            return;
        }

        if (xs->toplevel && !xs->toplevel->mapped && s.hasContent && xs->acked) {
            xs->toplevel->mapped = true;
            s.srv->scene->needsFrame = true;
            sysO << "imway: toplevel "_sv << sv(xs->toplevel->title) << " ("_sv << sv(xs->toplevel->appId) << ") mapped "_sv << s.width << "x"_sv << s.height << endL;

            s.srv->seat.focusToplevel(xs->toplevel);
        }

        if (xs->toplevel && xs->toplevel->mapped && !s.hasContent) {
            xs->toplevel->mapped = false;
            s.srv->scene->needsFrame = true;
            sysO << "imway: toplevel "_sv << sv(xs->toplevel->title) << " unmapped"_sv << endL;

            forEach<Popup>(s.srv->scene->popups, [&](Popup& popup) {
                if (popup.parent == &s && popup.mapped) {
                    dismissPopupTree((PopupImpl&)popup);
                }
            });

            s.srv->seat.toplevelUnmapped(xs->toplevel);

            // unmap returns the xdg_surface to its pre-initial-commit state:
            // the next commit is an initial commit again and must be answered
            // with a fresh configure, which the client must ack before it can
            // map — without this a spec-following remap hangs forever
            xs->initialConfigureSent = false;
            xs->acked = false;
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

            forEach<Popup>(s.srv->scene->popups, [&](Popup& popup) {
                if (popup.parent == &s && popup.mapped) {
                    dismissPopupTree((PopupImpl&)popup);
                }
            });
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

    void dismissPopupTree(PopupImpl& popup) {
        forEachRev<Popup>(popup.srv->scene->popups, [&](Popup& child) {
            if (&child != &popup && child.parent == popup.surface) {
                dismissPopupTree((PopupImpl&)child);
            }
        });

        xdgPopupDismiss(popup);
    }

    void outputRelease(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    const struct wl_output_interface outputImpl = {.release = outputRelease};

    void outputResourceDestroyed(wl_resource* res) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);

        removeOne(srv->outputResources, res);

        forEach<Surface, SceneNode>(srv->scene->surfaces, [&](Surface& surface) {
            removeOne(((SurfaceImpl&)surface).enteredOutputs, res);
        });
    }

    bool surfaceVisibleOnOutput(SurfaceImpl& surface) {
        if (!surface.contentMappedThroughAncestors()) {
            return false;
        }

        auto* root = (SurfaceImpl*)surface.rootSurface();

        if (root->toplevel && root->toplevel->mapped) {
            return true;
        }

        if (root->xdg && root->xdg->popup && root->xdg->popup->mapped) {
            return true;
        }

        return surface.srv->scene->cursorSurface == root ||
               surface.srv->scene->dragIcon == root;
    }

    void syncSurfaceOutputs(SurfaceImpl& surface) {
        bool visible = surfaceVisibleOnOutput(surface);

        if (!visible) {
            for (wl_resource* output : surface.enteredOutputs) {
                wl_surface_send_leave(surface.res, output);
            }

            surface.enteredOutputs.clear();

            return;
        }

        wl_client* client = wl_resource_get_client(surface.res);

        for (wl_resource* output : surface.srv->outputResources) {
            if (wl_resource_get_client(output) == client &&
                !contains(surface.enteredOutputs, output)) {
                wl_surface_send_enter(surface.res, output);
                surface.enteredOutputs.pushBack(output);
            }
        }
    }

    void outputBind(wl_client* client, void* data, u32 version, u32 id) {
        auto* srv = (WaylandImpl*)data;
        wl_resource* res = wl_resource_create(client, &wl_output_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &outputImpl, srv, outputResourceDestroyed);
        srv->outputResources.pushBack(res);
        srv->scene->needsFrame = true;

        Buffer make(srv->output ? srv->output->make() : "imway"_sv);
        Buffer model(srv->output ? srv->output->model() : "unknown"_sv);
        Buffer name(srv->output ? srv->output->outputName() : "UNKNOWN-1"_sv);

        wl_output_send_geometry(res, 0, 0, srv->output ? srv->output->physicalWidthMm() : 0,
                                srv->output ? srv->output->physicalHeightMm() : 0,
                                WL_OUTPUT_SUBPIXEL_UNKNOWN, make.cStr(), model.cStr(), WL_OUTPUT_TRANSFORM_NORMAL);
        wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, srv->scene->outW, srv->scene->outH, (i32)(srv->scene->hz * 1000));

        if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
            wl_output_send_scale(res, 1);
        }

        if (version >= WL_OUTPUT_NAME_SINCE_VERSION) {
            wl_output_send_name(res, name.cStr());
        }

        if (version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION) {
            auto& description = sb();

            description << sv(make) << " "_sv << sv(model);
            Buffer text(sv(description));

            wl_output_send_description(res, text.cStr());
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
            constexpr u32 valid = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;

            if (actions & ~valid) {
                wl_resource_post_error(res, WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                                       "drag action mask contains unknown bits");

                return;
            }

            if (src->actionsSet || src->usedForSelection || src->usedForDrag) {
                wl_resource_post_error(res, WL_DATA_SOURCE_ERROR_INVALID_SOURCE,
                                       "set_actions is only valid once before start_drag");

                return;
            }

            src->dndActions = actions;
            src->actionsSet = true;
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

        forEach<Offer>(src->offers, [](Offer& offer) {
            offer.source = nullptr;
        });

        // the orphans keep a valid ring among themselves for their unlinks
        src->offers.clear();
        src->srv->seat.sourceGone(src);
        src->srv->alloc->release(src);
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

    Offer* offerFrom(wl_resource* res) {
        return res ? (Offer*)wl_resource_get_user_data(res) : nullptr;
    }

    void offerAccept(wl_client*, wl_resource* res, u32, const char* mime) {
        Offer* offer = offerFrom(res);
        DataSource* src = offer ? offer->source : nullptr;

        if (offer && offer->finished) {
            wl_resource_post_error(res, WL_DATA_OFFER_ERROR_INVALID_OFFER,
                                   "offer was already finished");

            return;
        }

        if (offer) {
            offer->accepted = mime != nullptr;
        }

        if (src && offer->dnd && !src->primary) {
            wl_data_source_send_target(src->res, mime);
        }
    }

    void offerReceive(wl_client*, wl_resource* res, const char* mime, i32 fd) {
        Offer* offer = offerFrom(res);
        DataSource* src = offer ? offer->source : nullptr;

        if (offer && offer->finished) {
            close(fd);
            wl_resource_post_error(res, WL_DATA_OFFER_ERROR_INVALID_OFFER,
                                   "offer was already finished");

            return;
        }

        if (src) {
            if (src->dc) {
                ext_data_control_source_v1_send_send(src->res, mime, fd);
            } else if (src->primary) {
                zwp_primary_selection_source_v1_send_send(src->res, mime, fd);
            } else {
                wl_data_source_send_send(src->res, mime, fd);
            }
        }

        close(fd);
    }

    void offerFinish(wl_client*, wl_resource* res) {
        Offer* offer = offerFrom(res);
        DataSource* src = offer ? offer->source : nullptr;

        if (!offer || !offer->dnd || offer->finished || !src || !src->dropPerformed ||
            !offer->accepted || offer->action == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) {
            wl_resource_post_error(res, WL_DATA_OFFER_ERROR_INVALID_FINISH,
                                   "finish is not valid for this offer state");

            return;
        }

        offer->finished = true;

        if (wl_resource_get_version(src->res) >= 3) {
            wl_data_source_send_dnd_finished(src->res);
        }
    }

    void offerSetActions(wl_client*, wl_resource* res, u32 actions, u32 preferred) {
        Offer* offer = offerFrom(res);
        DataSource* src = offer ? offer->source : nullptr;
        constexpr u32 valid = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                              WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |
                              WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;

        if (!offer || !offer->dnd || offer->finished || !src || src->primary) {
            wl_resource_post_error(res, WL_DATA_OFFER_ERROR_INVALID_OFFER,
                                   "set_actions requires an active drag offer");

            return;
        }

        if (actions & ~valid) {
            wl_resource_post_error(res, WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
                                   "drag action mask contains unknown bits");

            return;
        }

        if (preferred != WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE &&
            preferred != WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY &&
            preferred != WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE &&
            preferred != WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK) {
            wl_resource_post_error(res, WL_DATA_OFFER_ERROR_INVALID_ACTION,
                                   "preferred drag action must contain one bit");

            return;
        }

        u32 available = actions & src->dndActions;

        if (preferred && !(preferred & available)) {
            wl_resource_post_error(res, WL_DATA_OFFER_ERROR_INVALID_ACTION,
                                   "preferred drag action is unavailable");

            return;
        }

        u32 action = preferred ? preferred : chooseDndAction(available);

        offer->action = action;

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
        Offer* offer = offerFrom(res);

        if (!offer) {
            return;
        }

        offer->unlink();
        offer->srv->alloc->release(offer);
    }

    const struct zwp_primary_selection_offer_v1_interface primaryOfferImpl = {
        .receive = offerReceive,
        .destroy = resDestroy,
    };

    wl_resource* makeDcOffer(wl_resource* device, DataSource* src);

    wl_resource* makeOffer(wl_resource* device, DataSource* src, bool dnd) {
        wl_client* client = wl_resource_get_client(device);
        u32 version = (u32)wl_resource_get_version(device);
        wl_resource* resource;
        Offer* offer = src->srv->alloc->make<Offer>();

        offer->srv = src->srv;
        offer->source = src;
        offer->dnd = dnd;
        src->offers.pushFront(offer);

        if (src->primary) {
            resource = wl_resource_create(client, &zwp_primary_selection_offer_v1_interface, (int)version, 0);

            if (!resource) {
                offer->unlink();
                src->srv->alloc->release(offer);

                return nullptr;
            }

            offer->res = resource;
            wl_resource_set_implementation(resource, &primaryOfferImpl, offer, offerResourceDestroyed);
            zwp_primary_selection_device_v1_send_data_offer(device, resource);

            for (const Mime& m : src->mimes) {
                zwp_primary_selection_offer_v1_send_offer(resource, m.s);
            }

            return resource;
        }

        resource = wl_resource_create(client, &wl_data_offer_interface, (int)version, 0);

        if (!resource) {
            offer->unlink();
            src->srv->alloc->release(offer);

            return nullptr;
        }

        offer->res = resource;
        wl_resource_set_implementation(resource, &dataOfferImpl, offer, offerResourceDestroyed);
        wl_data_device_send_data_offer(device, resource);

        for (const Mime& m : src->mimes) {
            wl_data_offer_send_offer(resource, m.s);
        }

        if (dnd && version >= 3) {
            wl_data_offer_send_source_actions(resource, src->dndActions);
        }

        return resource;
    }

    void deviceStartDrag(wl_client* client, wl_resource* res, wl_resource* sourceRes, wl_resource* originRes, wl_resource* iconRes, u32 serial) {
        auto* seat = (SeatState*)wl_resource_get_user_data(res);
        DataSource* src = sourceFrom(sourceRes);

        if (!seat || !originRes) {
            return;
        }

        SurfaceImpl* origin = surfaceFrom(originRes);

        if (seat->buttonsDown <= 0 || serial != seat->pointerGrabSerial ||
            client != seat->pointerGrabClient || origin != seat->pointerGrabOrigin) {
            // cancelled is a v1 event (v3 only added dnd_drop_performed and
            // friends); gating it left old clients waiting forever
            if (src) {
                wl_data_source_send_cancelled(src->res);
            }

            return;
        }

        if (src && (src->usedForDrag || src->usedForSelection)) {
            wl_resource_post_error(res, WL_DATA_DEVICE_ERROR_USED_SOURCE,
                                   "data source was already used");

            return;
        }

        SurfaceImpl* icon = iconRes ? surfaceFrom(iconRes) : nullptr;

        if (icon && icon->role != SurfaceRole::none && icon->role != SurfaceRole::dragIcon) {
            wl_resource_post_error(res, WL_DATA_DEVICE_ERROR_ROLE, "drag icon surface already has another role");

            return;
        }

        if (icon) {
            icon->role = SurfaceRole::dragIcon;
        }

        if (src) {
            src->usedForDrag = true;
        }

        seat->startDrag(client, serial, src, origin, icon);
    }

    void deviceSetSelection(wl_client* client, wl_resource* res, wl_resource* sourceRes, u32 serial) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            DataSource* src = sourceFrom(sourceRes);

            if (!seat->validSelectionSerial(client, serial)) {
                return;
            }

            if (src && (src->usedForDrag || src->usedForSelection)) {
                wl_resource_post_error(res, WL_DATA_DEVICE_ERROR_USED_SOURCE,
                                       "data source was already used");

                return;
            }

            if (src && src->actionsSet) {
                wl_resource_post_error(src->res, WL_DATA_SOURCE_ERROR_INVALID_SOURCE,
                                       "drag data source cannot become a selection");

                return;
            }

            if (src) {
                src->usedForSelection = true;
            }

            seat->setSelection(client, serial, src, false);
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

        DataSource* src = srv->alloc->make<DataSource>();

        src->srv = srv;
        src->res = s;
        src->primary = false;
        src->mimes.clear();
        src->dndActions = 0;
        src->dropPerformed = false;
        src->actionsSet = false;
        src->usedForDrag = false;
        src->usedForSelection = false;
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

        // a device bound while its client already holds keyboard focus (a
        // terminal binding on the first Ctrl+V) must learn the current
        // selection now, not at the next focus change
        wl_resource* target = srv->seat.kbTargetRes();

        if (target && wl_resource_get_client(target) == client) {
            wl_data_device_send_selection(d, srv->seat.clipboard ? makeOffer(d, srv->seat.clipboard, false) : nullptr);
        }
    }

    const struct wl_data_device_manager_interface dataManagerImpl = {
        .create_data_source = managerCreateDataSource,
        .get_data_device = managerGetDataDevice,
        // v4: destroying the manager leaves already-created data devices and
        // sources intact
        .release = resDestroy,
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

    void primaryDeviceSetSelection(wl_client* client, wl_resource* res, wl_resource* sourceRes, u32 serial) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            seat->setSelection(client, serial, sourceFrom(sourceRes), true);
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

        DataSource* src = srv->alloc->make<DataSource>();

        src->srv = srv;
        src->res = s;
        src->primary = true;
        src->mimes.clear();
        src->dndActions = 0;
        src->dropPerformed = false;
        src->actionsSet = false;
        src->usedForDrag = false;
        src->usedForSelection = false;
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

        // same late-bind catch-up as managerGetDataDevice
        wl_resource* target = srv->seat.kbTargetRes();

        if (target && wl_resource_get_client(target) == client) {
            zwp_primary_selection_device_v1_send_selection(d, srv->seat.primarySel ? makeOffer(d, srv->seat.primarySel, false) : nullptr);
        }
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

    void decoSetMode(wl_client*, wl_resource* res, u32 mode) {
        if (!zxdg_toplevel_decoration_v1_mode_is_valid(mode, wl_resource_get_version(res)) ||
            (mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE &&
             mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE)) {
            wl_resource_post_error(res, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_INVALID_MODE,
                                   "invalid decoration mode");

            return;
        }

        zxdg_toplevel_decoration_v1_send_configure(res, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    void decoUnsetMode(wl_client*, wl_resource* res) {
        zxdg_toplevel_decoration_v1_send_configure(res, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    void decoResourceDestroyed(wl_resource* res) {
        // the client dropped the decoration object: back to csd per spec
        if (auto* t = (ToplevelImpl*)wl_resource_get_user_data(res)) {
            if (t->decoRes == res) {
                t->decoRes = nullptr;
            }

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

        if (t->decoRes) {
            wl_resource_set_implementation(d, &decoImpl, nullptr, nullptr);
            wl_resource_post_error(d, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED,
                                   "xdg_toplevel already has a decoration object");

            return;
        }

        // we always answer SERVER_SIDE, so negotiating at all means our bar
        t->csd = false;
        t->srv->scene->needsFrame = true;
        t->decoRes = d;

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
            s->pendSrcSet = true;

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
        s->pendSrcSet = true;
    }

    void viewportSetDestination(wl_client*, wl_resource* res, i32 w, i32 h) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_NO_SURFACE, "surface is gone");

            return;
        }

        if (w == -1 && h == -1) {
            s->pendDw = s->pendDh = -1;
            s->pendDstSet = true;

            return;
        }

        if (w <= 0 || h <= 0) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_BAD_VALUE, "invalid destination");

            return;
        }

        s->pendDw = w;
        s->pendDh = h;
        s->pendDstSet = true;
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
        s->pendSrcSet = s->pendDstSet = true;
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
        if (s.pendSrcSet) {
            s.vp.hasSrc = s.pendSw > 0;

            if (s.vp.hasSrc) {
                s.vp.sx = s.pendSx;
                s.vp.sy = s.pendSy;
                s.vp.sw = s.pendSw;
                s.vp.sh = s.pendSh;
            }

            s.pendSrcSet = false;
        }

        if (s.pendDstSet) {
            s.vp.hasDst = s.pendDw > 0;

            if (s.vp.hasDst) {
                s.vp.dw = s.pendDw;
                s.vp.dh = s.pendDh;
            }

            s.pendDstSet = false;
        }
    }

    void viewportSurfaceGone(SurfaceImpl& s) {
        if (s.vpRes) {
            wl_resource_set_user_data(s.vpRes, nullptr);
        }
    }

    // ---- xdg-dialog ----
    void xdgDialogSetModal(wl_client*, wl_resource* res, bool modal) {
        if (auto* t = (ToplevelImpl*)wl_resource_get_user_data(res)) {
            t->modal = modal;
            t->srv->scene->needsFrame = true;
        }
    }

    void xdgDialogSet(wl_client*, wl_resource* res) {
        xdgDialogSetModal(nullptr, res, true);
    }

    void xdgDialogUnset(wl_client*, wl_resource* res) {
        xdgDialogSetModal(nullptr, res, false);
    }

    void xdgDialogResourceDestroyed(wl_resource* res) {
        if (auto* t = (ToplevelImpl*)wl_resource_get_user_data(res)) {
            t->modal = false;
            t->dialogRes = nullptr;
        }
    }

    const struct xdg_dialog_v1_interface xdgDialogImpl = {
        .destroy = resDestroy,
        .set_modal = xdgDialogSet,
        .unset_modal = xdgDialogUnset,
    };

    void xdgWmDialogGetDialog(wl_client* client, wl_resource* res, u32 id, wl_resource* toplevelRes) {
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(toplevelRes);
        wl_resource* d = wl_resource_create(client, &xdg_dialog_v1_interface, wl_resource_get_version(res), id);

        if (!d) {
            wl_client_post_no_memory(client);

            return;
        }

        t->dialogRes = d;
        wl_resource_set_implementation(d, &xdgDialogImpl, t, xdgDialogResourceDestroyed);
    }

    const struct xdg_wm_dialog_v1_interface xdgWmDialogImpl = {
        .destroy = resDestroy,
        .get_xdg_dialog = xdgWmDialogGetDialog,
    };

    void xdgWmDialogBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &xdg_wm_dialog_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &xdgWmDialogImpl, data, nullptr);
    }

    // ---- wp-pointer-warp ----
    void pointerWarp(wl_client* client, wl_resource* res, wl_resource* surfaceRes,
                     wl_resource* pointerRes, wl_fixed_t x, wl_fixed_t y, u32 serial) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        // only the focused surface, with a serial that matches its pointer
        // enter, may place the cursor — and only within the surface
        if (!s || srv->seat.ptrFocus != s ||
            !srv->seat.validPointerEnter(pointerRes, serial) ||
            wl_resource_get_client(resOf(s)) != client) {
            return;
        }

        double lx = wl_fixed_to_double(x);
        double ly = wl_fixed_to_double(y);

        if (lx < 0 || ly < 0 || lx >= s->viewW() || ly >= s->viewH()) {
            return;
        }

        // inject an absolute motion through the same sink chain a real device
        // uses: the renderer moves its cursor, the seat re-delivers motion
        PointerMotionEvent ev;

        ev.x = s->imgX + lx;
        ev.y = s->imgY + ly;

        if (srv->composer && srv->composer->entry) {
            srv->composer->entry->pointerMotion(ev);
        }
    }

    const struct wp_pointer_warp_v1_interface pointerWarpImpl = {
        .destroy = resDestroy,
        .warp_pointer = pointerWarp,
    };

    void pointerWarpBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_pointer_warp_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &pointerWarpImpl, data, nullptr);
    }

    // ---- wp-tearing-control ----
    void tearingSetHint(wl_client*, wl_resource* res, u32 hint) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->pendTearingAsync = hint == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;
            s->pendTearingChanged = true;
        }
    }

    void tearingResourceDestroyed(wl_resource* res) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->tearingRes = nullptr;
            s->pendTearingAsync = false;
            s->pendTearingChanged = true;
        }
    }

    const struct wp_tearing_control_v1_interface tearingImpl = {
        .set_presentation_hint = tearingSetHint,
        .destroy = resDestroy,
    };

    void tearingManagerGetControl(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->tearingRes) {
            wl_resource_post_error(res, WP_TEARING_CONTROL_MANAGER_V1_ERROR_TEARING_CONTROL_EXISTS,
                                   "surface already has a tearing control");

            return;
        }

        wl_resource* r = wl_resource_create(client, &wp_tearing_control_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        s->tearingRes = r;
        wl_resource_set_implementation(r, &tearingImpl, s, tearingResourceDestroyed);
    }

    const struct wp_tearing_control_manager_v1_interface tearingManagerImpl = {
        .destroy = resDestroy,
        .get_tearing_control = tearingManagerGetControl,
    };

    void tearingManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_tearing_control_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &tearingManagerImpl, data, nullptr);
    }

    void tearingSurfaceGone(SurfaceImpl& s) {
        if (s.tearingRes) {
            wl_resource_set_user_data(s.tearingRes, nullptr);
        }
    }

    // ---- wp-fifo ----
    void fifoSetBarrier(wl_client*, wl_resource* res) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_FIFO_V1_ERROR_SURFACE_DESTROYED, "the wl_surface is gone");

            return;
        }

        s->pending.fifoSet = true;
    }

    void fifoWaitBarrier(wl_client*, wl_resource* res) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_FIFO_V1_ERROR_SURFACE_DESTROYED, "the wl_surface is gone");

            return;
        }

        s->pending.fifoWait = true;
    }

    void fifoResourceDestroyed(wl_resource* res) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->fifo->fifoRes = nullptr;
        }
    }

    const struct wp_fifo_v1_interface fifoImpl = {
        .set_barrier = fifoSetBarrier,
        .wait_barrier = fifoWaitBarrier,
        .destroy = resDestroy,
    };

    void fifoManagerGetFifo(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->fifo && s->fifo->fifoRes) {
            wl_resource_post_error(res, WP_FIFO_MANAGER_V1_ERROR_ALREADY_EXISTS, "surface already has a fifo object");

            return;
        }

        wl_resource* r = wl_resource_create(client, &wp_fifo_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        if (!s->fifo) {
            s->fifo = s->srv->alloc->make<FifoState>();
        }

        s->fifo->fifoRes = r;
        wl_resource_set_implementation(r, &fifoImpl, s, fifoResourceDestroyed);
    }

    const struct wp_fifo_manager_v1_interface fifoManagerImpl = {
        .destroy = resDestroy,
        .get_fifo = fifoManagerGetFifo,
    };

    void fifoManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_fifo_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &fifoManagerImpl, data, nullptr);
    }

    void fifoSurfaceGone(SurfaceImpl& s) {
        if (!s.fifo) {
            return;
        }

        if (s.fifo->fifoRes) {
            wl_resource_set_user_data(s.fifo->fifoRes, nullptr);
        }

        if (s.fifo->commitTimerRes) {
            wl_resource_set_user_data(s.fifo->commitTimerRes, nullptr);
        }

        while (!s.fifo->queue.empty()) {
            FifoEntry* e = (FifoEntry*)s.fifo->queue.popFront();
            Vector<wl_resource*> cbs;

            cbs.xchg(e->cache.frames);

            for (wl_resource* cb : cbs) {
                wl_resource_destroy(cb);
            }

            releaseCachedDmabuf(s.srv, e->cache);
            s.srv->alloc->release(e);
        }

        s.srv->alloc->release(s.fifo);
        s.fifo = nullptr;
    }

    // ---- wp-commit-timing ----
    void commitTimerSetTimestamp(wl_client*, wl_resource* res, u32 hi, u32 lo, u32 nsec) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_COMMIT_TIMER_V1_ERROR_SURFACE_DESTROYED, "the wl_surface is gone");

            return;
        }

        if (nsec >= 1000000000u) {
            wl_resource_post_error(res, WP_COMMIT_TIMER_V1_ERROR_INVALID_TIMESTAMP, "tv_nsec out of range");

            return;
        }

        if (s->pending.timeSet) {
            wl_resource_post_error(res, WP_COMMIT_TIMER_V1_ERROR_TIMESTAMP_EXISTS, "this commit already has a timestamp");

            return;
        }

        u64 sec = ((u64)hi << 32) | lo;
        // saturate instead of overflowing: half a millennium out is "never"
        u64 ns = sec > 18446744073u ? ~0ull : sec * 1000000000ull + nsec;

        s->pending.timeSet = true;
        s->pending.timeNs = ns;
    }

    void commitTimerResourceDestroyed(wl_resource* res) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->fifo->commitTimerRes = nullptr;
        }
    }

    const struct wp_commit_timer_v1_interface commitTimerImpl = {
        .set_timestamp = commitTimerSetTimestamp,
        .destroy = resDestroy,
    };

    void commitTimingGetTimer(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->fifo && s->fifo->commitTimerRes) {
            wl_resource_post_error(res, WP_COMMIT_TIMING_MANAGER_V1_ERROR_COMMIT_TIMER_EXISTS, "surface already has a commit timer");

            return;
        }

        wl_resource* r = wl_resource_create(client, &wp_commit_timer_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        if (!s->fifo) {
            s->fifo = s->srv->alloc->make<FifoState>();
        }

        s->fifo->commitTimerRes = r;
        wl_resource_set_implementation(r, &commitTimerImpl, s, commitTimerResourceDestroyed);
    }

    const struct wp_commit_timing_manager_v1_interface commitTimingManagerImpl = {
        .destroy = resDestroy,
        .get_timer = commitTimingGetTimer,
    };

    void commitTimingManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_commit_timing_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &commitTimingManagerImpl, data, nullptr);
    }

    // ---- ext-data-control ----
    // a privileged clipboard manager: no focus, no serials. Sources, offers
    // and the loopback share the DataSource/Offer machinery via the dc flag
    void dcOfferReceive(wl_client*, wl_resource* res, const char* mime, i32 fd) {
        Offer* offer = (Offer*)wl_resource_get_user_data(res);
        DataSource* src = offer ? offer->source : nullptr;

        if (src && src->dc) {
            ext_data_control_source_v1_send_send(src->res, mime, fd);
        }

        close(fd);
    }

    const struct ext_data_control_offer_v1_interface dcOfferImpl = {
        .receive = dcOfferReceive,
        .destroy = resDestroy,
    };

    wl_resource* makeDcOffer(wl_resource* device, DataSource* src) {
        wl_client* client = wl_resource_get_client(device);
        u32 version = (u32)wl_resource_get_version(device);
        wl_resource* resource = wl_resource_create(client, &ext_data_control_offer_v1_interface, (int)version, 0);

        if (!resource) {
            return nullptr;
        }

        Offer* offer = src->srv->alloc->make<Offer>();

        offer->srv = src->srv;
        offer->source = src;
        offer->dc = true;
        src->offers.pushFront(offer);
        offer->res = resource;
        wl_resource_set_implementation(resource, &dcOfferImpl, offer, offerResourceDestroyed);
        ext_data_control_device_v1_send_data_offer(device, resource);

        for (const Mime& m : src->mimes) {
            ext_data_control_offer_v1_send_offer(resource, m.s);
        }

        return resource;
    }

    void dcSourceOffer(wl_client*, wl_resource* res, const char* mime) {
        DataSource* src = (DataSource*)wl_resource_get_user_data(res);
        StringView mv(mime);

        if (!src || src->usedForSelection || src->mimes.length() >= 64 ||
            mv.length() >= sizeof(Mime::s)) {
            return;
        }

        Mime m;

        memcpy(m.s, mv.data(), mv.length());
        m.s[mv.length()] = 0;
        src->mimes.pushBack(m);
    }

    const struct ext_data_control_source_v1_interface dcSourceImpl = {
        .offer = dcSourceOffer,
        .destroy = resDestroy,
    };

    void dcSourceResourceDestroyed(wl_resource* res) {
        if (DataSource* src = (DataSource*)wl_resource_get_user_data(res)) {
            src->srv->seat.sourceGone(src);
            src->srv->alloc->release(src);
        }
    }

    void dcManagerCreateSource(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &ext_data_control_source_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        DataSource* src = srv->alloc->make<DataSource>();

        src->srv = srv;
        src->res = r;
        src->dc = true;
        wl_resource_set_implementation(r, &dcSourceImpl, src, dcSourceResourceDestroyed);
    }

    void dcDeviceSetSelection(wl_client*, wl_resource* res, wl_resource* sourceRes, bool primary) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        DataSource* src = sourceRes ? (DataSource*)wl_resource_get_user_data(sourceRes) : nullptr;

        if (src) {
            if (src->usedForSelection || src->usedForDrag) {
                wl_resource_post_error(res, EXT_DATA_CONTROL_DEVICE_V1_ERROR_USED_SOURCE, "source already used");

                return;
            }

            src->usedForSelection = true;
        }

        srv->seat.applySelection(src, primary);
    }

    void dcDeviceSetSelectionReq(wl_client* c, wl_resource* res, wl_resource* sourceRes) {
        dcDeviceSetSelection(c, res, sourceRes, false);
    }

    void dcDeviceSetPrimaryReq(wl_client* c, wl_resource* res, wl_resource* sourceRes) {
        dcDeviceSetSelection(c, res, sourceRes, true);
    }

    void dcDeviceResourceDestroyed(wl_resource* res) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);

        removeOne(srv->seat.dcDevices, res);
    }

    const struct ext_data_control_device_v1_interface dcDeviceImpl = {
        .set_selection = dcDeviceSetSelectionReq,
        .destroy = resDestroy,
        .set_primary_selection = dcDeviceSetPrimaryReq,
    };

    void dcManagerGetDevice(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &ext_data_control_device_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(r, &dcDeviceImpl, srv, dcDeviceResourceDestroyed);
        srv->seat.dcDevices.pushBack(r);
        // seed the new device with the current selections
        ext_data_control_device_v1_send_selection(
            r, srv->seat.clipboard ? makeDcOffer(r, srv->seat.clipboard) : nullptr);
        ext_data_control_device_v1_send_primary_selection(
            r, srv->seat.primarySel ? makeDcOffer(r, srv->seat.primarySel) : nullptr);
    }

    const struct ext_data_control_manager_v1_interface dcManagerImpl = {
        .create_data_source = dcManagerCreateSource,
        .get_data_device = dcManagerGetDevice,
        .destroy = resDestroy,
    };

    void dcManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &ext_data_control_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &dcManagerImpl, data, nullptr);
    }

    // ---- text-input-v3 / input-method-v2 / virtual-keyboard-v1 ----
    static void copyText(StringBuilder& out, const char* s) {
        out.reset();

        if (s) {
            out << StringView(s);
        }
    }

    void textInputResourceDestroyed(wl_resource* res) {
        auto* ti = (TextInput*)wl_resource_get_user_data(res);
        bool wasActive = ti->srv->seat.activeTextInput() == ti;

        ti->unlink();

        if (wasActive) {
            ti->srv->seat.imUpdateActivation();
        }

        ti->srv->alloc->release(ti);
    }

    void textInputEnable(wl_client*, wl_resource* res) {
        auto* ti = (TextInput*)wl_resource_get_user_data(res);

        ti->pendingEnabled = true;
        ti->pendingSurroundingSet = false;
        ti->pendingContentSet = false;
        ti->pendingRectSet = false;
        ti->pendingChangeCause = 0;
    }

    void textInputDisable(wl_client*, wl_resource* res) {
        auto* ti = (TextInput*)wl_resource_get_user_data(res);

        ti->pendingEnabled = false;
    }

    void textInputSetSurrounding(wl_client*, wl_resource* res, const char* text, i32 cursor, i32 anchor) {
        auto* ti = (TextInput*)wl_resource_get_user_data(res);

        ti->pendingSurroundingSet = true;
        copyText(ti->pendingSurrounding, text);
        ti->pendingCursor = cursor;
        ti->pendingAnchor = anchor;
    }

    void textInputSetChangeCause(wl_client*, wl_resource* res, u32 cause) {
        auto* ti = (TextInput*)wl_resource_get_user_data(res);

        ti->pendingChangeCause = cause;
    }

    void textInputSetContentType(wl_client*, wl_resource* res, u32 hint, u32 purpose) {
        auto* ti = (TextInput*)wl_resource_get_user_data(res);

        ti->pendingContentSet = true;
        ti->pendingHint = hint;
        ti->pendingPurpose = purpose;
    }

    void textInputSetCursorRectangle(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        auto* ti = (TextInput*)wl_resource_get_user_data(res);

        ti->pendingRectSet = true;
        ti->pendingRect = {x, y, w, h};
    }

    void textInputCommit(wl_client*, wl_resource* res) {
        auto* ti = (TextInput*)wl_resource_get_user_data(res);
        bool wasEnabled = ti->enabled;

        ti->enabled = ti->pendingEnabled;

        if (ti->pendingSurroundingSet) {
            ti->surroundingSet = true;
            copyText(ti->surrounding, Buffer(sv(ti->pendingSurrounding)).cStr());
            ti->cursor = ti->pendingCursor;
            ti->anchor = ti->pendingAnchor;
        }

        if (ti->pendingContentSet) {
            ti->contentSet = true;
            ti->hint = ti->pendingHint;
            ti->purpose = ti->pendingPurpose;
        }

        if (ti->pendingRectSet) {
            ti->rectSet = true;
            ti->rect = ti->pendingRect;
        }

        ti->changeCause = ti->pendingChangeCause;

        if (!ti->enabled) {
            ti->surroundingSet = ti->contentSet = ti->rectSet = false;
        }

        (void)wasEnabled;
        ti->srv->seat.imUpdateActivation();
    }

    const struct zwp_text_input_v3_interface textInputImpl = {
        .destroy = resDestroy,
        .enable = textInputEnable,
        .disable = textInputDisable,
        .set_surrounding_text = textInputSetSurrounding,
        .set_text_change_cause = textInputSetChangeCause,
        .set_content_type = textInputSetContentType,
        .set_cursor_rectangle = textInputSetCursorRectangle,
        .commit = textInputCommit,
    };

    void textInputManagerGetTextInput(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &zwp_text_input_v3_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        TextInput* ti = srv->alloc->make<TextInput>();

        ti->srv = srv;
        ti->res = r;
        ti->client = client;
        srv->seat.textInputs.pushBack(ti);
        wl_resource_set_implementation(r, &textInputImpl, ti, textInputResourceDestroyed);

        // if this client already holds keyboard focus, it enters immediately
        if (wl_resource* target = srv->seat.kbTargetRes()) {
            if (wl_resource_get_client(target) == client) {
                zwp_text_input_v3_send_enter(r, target);
            }
        }
    }

    const struct zwp_text_input_manager_v3_interface textInputManagerImpl = {
        .destroy = resDestroy,
        .get_text_input = textInputManagerGetTextInput,
    };

    void textInputManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_text_input_manager_v3_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &textInputManagerImpl, data, nullptr);
    }

    // -- input-method-v2 --
    void imKeyboardGrabRelease(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void imKeyboardGrabResourceDestroyed(wl_resource* res) {
        auto* im = (InputMethod*)wl_resource_get_user_data(res);

        if (im) {
            im->grab = nullptr;
        }
    }

    const struct zwp_input_method_keyboard_grab_v2_interface imKeyboardGrabImpl = {
        .release = imKeyboardGrabRelease,
    };

    void imPopupResourceDestroyed(wl_resource* res) {
        auto* im = (InputMethod*)wl_resource_get_user_data(res);

        if (!im) {
            return;
        }

        if (im->srv->scene->imePopup == (Surface*)im->popupSurface) {
            im->srv->scene->imePopup = nullptr;
        }

        im->popupSurface = nullptr;
        im->popupRes = nullptr;
    }

    const struct zwp_input_popup_surface_v2_interface imPopupImpl = {
        .destroy = resDestroy,
    };

    void imCommitString(wl_client*, wl_resource* res, const char* text) {
        auto* im = (InputMethod*)wl_resource_get_user_data(res);

        im->commitSet = true;
        copyText(im->commitStr, text);
    }

    void imSetPreedit(wl_client*, wl_resource* res, const char* text, i32 begin, i32 end) {
        auto* im = (InputMethod*)wl_resource_get_user_data(res);

        im->preeditSet = true;
        copyText(im->preeditStr, text);
        im->preeditBegin = begin;
        im->preeditEnd = end;
    }

    void imDeleteSurrounding(wl_client*, wl_resource* res, u32 before, u32 after) {
        auto* im = (InputMethod*)wl_resource_get_user_data(res);

        im->deleteBefore = before;
        im->deleteAfter = after;
    }

    void imCommit(wl_client*, wl_resource* res, u32) {
        auto* im = (InputMethod*)wl_resource_get_user_data(res);

        im->srv->seat.imFlushToTextInput();
    }

    void imGetPopupSurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        auto* im = (InputMethod*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &zwp_input_popup_surface_v2_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        im->popupSurface = surfaceRes ? surfaceFrom(surfaceRes) : nullptr;
        im->popupRes = r;
        im->lastRect = {-1, -1, -1, -1};
        wl_resource_set_implementation(r, &imPopupImpl, im, imPopupResourceDestroyed);

        // the rectangle and scene placement are pushed from the frame event
        im->srv->seat.imUpdatePopup();
    }

    void imGrabKeyboard(wl_client* client, wl_resource* res, u32 id) {
        auto* im = (InputMethod*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &zwp_input_method_keyboard_grab_v2_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        im->grab = r;
        wl_resource_set_implementation(r, &imKeyboardGrabImpl, im, imKeyboardGrabResourceDestroyed);

        SeatState& seat = im->srv->seat;

        zwp_input_method_keyboard_grab_v2_send_keymap(r, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
            seat.kb->keymapFd(), seat.kb->keymapSize());
        zwp_input_method_keyboard_grab_v2_send_modifiers(r, wl_display_next_serial(im->srv->display),
            seat.modsDepressed, seat.modsLatched, seat.modsLocked, seat.modsGroup);
        zwp_input_method_keyboard_grab_v2_send_repeat_info(r, 25, 600);
    }

    void imDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void imResourceDestroyed(wl_resource* res) {
        auto* im = (InputMethod*)wl_resource_get_user_data(res);
        WaylandImpl* srv = im->srv;

        // the grab may outlive us on a disconnect: sever its back-pointer so
        // its own destroy handler sees a dead input method
        if (im->grab) {
            wl_resource_set_user_data(im->grab, nullptr);
        }

        if (srv->seat.inputMethod == im) {
            srv->seat.inputMethod = nullptr;
        }

        srv->alloc->release(im);
    }

    const struct zwp_input_method_v2_interface inputMethodImpl = {
        .commit_string = imCommitString,
        .set_preedit_string = imSetPreedit,
        .delete_surrounding_text = imDeleteSurrounding,
        .commit = imCommit,
        .get_input_popup_surface = imGetPopupSurface,
        .grab_keyboard = imGrabKeyboard,
        .destroy = imDestroy,
    };

    void imManagerGetInputMethod(wl_client* client, wl_resource* res, wl_resource*, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &zwp_input_method_v2_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        if (srv->seat.inputMethod) {
            // one input method per seat: the second is inert
            InputMethod* tmp = srv->alloc->make<InputMethod>();

            tmp->srv = srv;
            tmp->res = r;
            wl_resource_set_implementation(r, &inputMethodImpl, tmp, imResourceDestroyed);
            zwp_input_method_v2_send_unavailable(r);

            return;
        }

        InputMethod* im = srv->alloc->make<InputMethod>();

        im->srv = srv;
        im->res = r;
        srv->seat.inputMethod = im;
        wl_resource_set_implementation(r, &inputMethodImpl, im, imResourceDestroyed);

        // activate straight away if a text input is already focused+enabled
        srv->seat.imUpdateActivation();
    }

    const struct zwp_input_method_manager_v2_interface imManagerImpl = {
        .get_input_method = imManagerGetInputMethod,
        .destroy = resDestroy,
    };

    void imManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_input_method_manager_v2_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &imManagerImpl, data, nullptr);
    }

    // -- virtual-keyboard-v1 --
    // the IME (or an a11y client) synthesizes key events; they flow into the
    // focused application through the normal seat path
    void vkKeymap(wl_client*, wl_resource*, u32, i32 fd, u32) {
        // we deliver through our own seat keymap; close the client's fd
        close(fd);
    }

    void vkKey(wl_client*, wl_resource* res, u32, u32 key, u32 state) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);

        srv->seat.handleKey(key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
    }

    void vkModifiers(wl_client*, wl_resource*, u32, u32, u32, u32) {
        // the shared xkb state mirrors physical keys; a virtual keyboard's
        // explicit modifier latch is not merged in this ring
    }

    const struct zwp_virtual_keyboard_v1_interface virtualKeyboardImpl = {
        .keymap = vkKeymap,
        .key = vkKey,
        .modifiers = vkModifiers,
        .destroy = resDestroy,
    };

    void vkManagerCreate(wl_client* client, wl_resource* res, wl_resource*, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &zwp_virtual_keyboard_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(r, &virtualKeyboardImpl, srv, nullptr);
    }

    const struct zwp_virtual_keyboard_manager_v1_interface vkManagerImpl = {
        .create_virtual_keyboard = vkManagerCreate,
    };

    void vkManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwp_virtual_keyboard_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &vkManagerImpl, data, nullptr);
    }

    // ---- ext-image-capture-source + ext-image-copy-capture ----
    const struct ext_image_capture_source_v1_interface captureSourceImpl = {
        .destroy = resDestroy,
    };

    void captureSourceManagerCreateSource(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &ext_image_capture_source_v1_interface, 1, id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        // one output: the source object only proves the client went through
        // the manager; srv rides along for the session constructors
        wl_resource_set_implementation(r, &captureSourceImpl, srv, nullptr);
    }

    const struct ext_output_image_capture_source_manager_v1_interface captureSourceManagerImpl = {
        .create_source = captureSourceManagerCreateSource,
        .destroy = resDestroy,
    };

    void captureSourceManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &ext_output_image_capture_source_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &captureSourceManagerImpl, data, nullptr);
    }

    void sendCaptureConstraints(WaylandImpl* srv, wl_resource* res, bool cursor) {
        int w = srv->scene->outW, h = srv->scene->outH;

        if (cursor) {
            Surface* cs = srv->scene->cursorSurface;

            w = cs && cs->viewW() > 0 ? cs->viewW() : 1;
            h = cs && cs->viewH() > 0 ? cs->viewH() : 1;
        }

        ext_image_copy_capture_session_v1_send_buffer_size(res, (u32)w, (u32)h);
        ext_image_copy_capture_session_v1_send_shm_format(res, WL_SHM_FORMAT_XRGB8888);
        ext_image_copy_capture_session_v1_send_done(res);
    }

    void captureFrameDrop(CaptureFrame* f) {
        if (f->bufferDestroyArmed) {
            wl_list_remove(&f->bufferDestroy.listener.link);
            f->bufferDestroyArmed = false;
        }

        f->buffer = nullptr;
    }

    void captureFrameResourceDestroyed(wl_resource* res) {
        auto* f = (CaptureFrame*)wl_resource_get_user_data(res);

        if (f->session) {
            f->session->frame = nullptr;
        }

        captureFrameDrop(f);
        f->srv->alloc->release(f);
    }

    void captureBufferDestroyed(wl_listener* l, void*) {
        CaptureFrame* f = ((CaptureBufferDestroyListener*)l)->frame;

        f->buffer = nullptr;
        f->bufferDestroyArmed = false;
        wl_list_remove(&f->bufferDestroy.listener.link);
    }

    void captureFrameAttachBuffer(wl_client*, wl_resource* res, wl_resource* buffer) {
        auto* f = (CaptureFrame*)wl_resource_get_user_data(res);

        if (f->captured) {
            wl_resource_post_error(res, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED, "the frame was already captured");

            return;
        }

        captureFrameDrop(f);
        f->buffer = buffer;
        f->bufferDestroy.listener.notify = captureBufferDestroyed;
        f->bufferDestroy.frame = f;
        wl_resource_add_destroy_listener(buffer, &f->bufferDestroy.listener);
        f->bufferDestroyArmed = true;
    }

    void captureFrameDamageBuffer(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        auto* f = (CaptureFrame*)wl_resource_get_user_data(res);

        if (f->captured) {
            wl_resource_post_error(res, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED, "the frame was already captured");

            return;
        }

        if (x < 0 || y < 0 || w <= 0 || h <= 0) {
            wl_resource_post_error(res, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_INVALID_BUFFER_DAMAGE, "invalid buffer damage");

            return;
        }

        // we always deliver full frames; damage is accepted, not tracked
    }

    void captureFrameCapture(wl_client*, wl_resource* res) {
        auto* f = (CaptureFrame*)wl_resource_get_user_data(res);

        if (f->captured) {
            wl_resource_post_error(res, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED, "the frame was already captured");

            return;
        }

        if (!f->buffer) {
            wl_resource_post_error(res, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_NO_BUFFER, "capture without an attached buffer");

            return;
        }

        f->captured = true;
        f->armed = true;
        f->attempts = 0;
        f->srv->scene->needsFrame = true;
    }

    const struct ext_image_copy_capture_frame_v1_interface captureFrameImpl = {
        .destroy = resDestroy,
        .attach_buffer = captureFrameAttachBuffer,
        .damage_buffer = captureFrameDamageBuffer,
        .capture = captureFrameCapture,
    };

    void captureSessionResourceDestroyed(wl_resource* res) {
        auto* cs = (CaptureSession*)wl_resource_get_user_data(res);

        if (cs->frame) {
            cs->frame->session = nullptr;
            cs->frame->armed = false;
        }

        cs->unlink();
        cs->srv->alloc->release(cs);
    }

    void captureSessionCreateFrame(wl_client* client, wl_resource* res, u32 id) {
        auto* cs = (CaptureSession*)wl_resource_get_user_data(res);

        if (cs->frame) {
            wl_resource_post_error(res, EXT_IMAGE_COPY_CAPTURE_SESSION_V1_ERROR_DUPLICATE_FRAME, "the session already has a frame");

            return;
        }

        wl_resource* r = wl_resource_create(client, &ext_image_copy_capture_frame_v1_interface, 1, id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        CaptureFrame* f = cs->srv->alloc->make<CaptureFrame>();

        f->srv = cs->srv;
        f->session = cs;
        f->res = r;
        cs->frame = f;
        wl_resource_set_implementation(r, &captureFrameImpl, f, captureFrameResourceDestroyed);
    }

    const struct ext_image_copy_capture_session_v1_interface captureSessionImpl = {
        .create_frame = captureSessionCreateFrame,
        .destroy = resDestroy,
    };

    CaptureSession* makeCaptureSession(WaylandImpl* srv, wl_client* client, u32 id, bool cursor) {
        wl_resource* r = wl_resource_create(client, &ext_image_copy_capture_session_v1_interface, 1, id);

        if (!r) {
            wl_client_post_no_memory(client);

            return nullptr;
        }

        CaptureSession* cs = srv->alloc->make<CaptureSession>();

        cs->srv = srv;
        cs->res = r;
        cs->cursor = cursor;
        srv->captureSessions.pushBack(cs);
        wl_resource_set_implementation(r, &captureSessionImpl, cs, captureSessionResourceDestroyed);
        sendCaptureConstraints(srv, r, cursor);

        return cs;
    }

    void captureManagerCreateSession(wl_client* client, wl_resource* res, u32 id, wl_resource* sourceRes, u32 options) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(sourceRes);

        if (options & ~(u32)EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS) {
            wl_resource_post_error(res, EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_ERROR_INVALID_OPTION, "unknown option flags");

            return;
        }

        // cursors are composited into the scene; paint_cursors is the truth
        // either way
        makeCaptureSession(srv, client, id, false);
    }

    void captureCursorSessionResourceDestroyed(wl_resource* res) {
        auto* cc = (CaptureCursorSession*)wl_resource_get_user_data(res);

        removeOne(cc->srv->cursorSessions, cc);
        cc->srv->alloc->release(cc);
    }

    void captureCursorGetSession(wl_client* client, wl_resource* res, u32 id) {
        auto* cc = (CaptureCursorSession*)wl_resource_get_user_data(res);

        if (cc->haveSession) {
            wl_resource_post_error(res, EXT_IMAGE_COPY_CAPTURE_CURSOR_SESSION_V1_ERROR_DUPLICATE_SESSION, "the cursor session was already created");

            return;
        }

        cc->haveSession = true;
        makeCaptureSession(cc->srv, client, id, true);
    }

    const struct ext_image_copy_capture_cursor_session_v1_interface captureCursorSessionImpl = {
        .destroy = resDestroy,
        .get_capture_session = captureCursorGetSession,
    };

    void captureManagerCreateCursorSession(wl_client* client, wl_resource* res, u32 id, wl_resource* sourceRes, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(sourceRes);
        wl_resource* r = wl_resource_create(client, &ext_image_copy_capture_cursor_session_v1_interface, 1, id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        (void)res;

        CaptureCursorSession* cc = srv->alloc->make<CaptureCursorSession>();

        cc->srv = srv;
        cc->res = r;
        srv->cursorSessions.pushBack(cc);
        wl_resource_set_implementation(r, &captureCursorSessionImpl, cc, captureCursorSessionResourceDestroyed);
    }

    const struct ext_image_copy_capture_manager_v1_interface captureManagerImpl = {
        .create_session = captureManagerCreateSession,
        .create_pointer_cursor_session = captureManagerCreateCursorSession,
        .destroy = resDestroy,
    };

    void captureManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &ext_image_copy_capture_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &captureManagerImpl, data, nullptr);
    }

    void captureSendReady(CaptureFrame& f, u32 w, u32 h) {
        u64 t = nowNs();
        u64 sec = t / 1000000000ull;

        ext_image_copy_capture_frame_v1_send_transform(f.res, WL_OUTPUT_TRANSFORM_NORMAL);
        ext_image_copy_capture_frame_v1_send_damage(f.res, 0, 0, (i32)w, (i32)h);
        ext_image_copy_capture_frame_v1_send_presentation_time(
            f.res, (u32)(sec >> 32), (u32)sec, (u32)(t % 1000000000ull));
        ext_image_copy_capture_frame_v1_send_ready(f.res);
    }

    void captureFail(CaptureFrame& f, u32 reason) {
        f.armed = false;
        ext_image_copy_capture_frame_v1_send_failed(f.res, reason);
    }

    void fulfillCapture(WaylandImpl* srv, CaptureSession& cs, CaptureFrame& f) {
        if (!f.buffer) {
            // the attached buffer died before the capture could happen
            captureFail(f, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);

            return;
        }

        wl_shm_buffer* shm = wl_shm_buffer_get(f.buffer);
        u32 wantW = cs.cursor ? 0 : (u32)srv->scene->outW;
        u32 wantH = cs.cursor ? 0 : (u32)srv->scene->outH;
        Surface* cur = srv->scene->cursorSurface;

        if (cs.cursor) {
            wantW = cur && cur->viewW() > 0 ? (u32)cur->viewW() : 1;
            wantH = cur && cur->viewH() > 0 ? (u32)cur->viewH() : 1;
        }

        if (!shm || wl_shm_buffer_get_format(shm) != WL_SHM_FORMAT_XRGB8888 ||
            (u32)wl_shm_buffer_get_width(shm) != wantW ||
            (u32)wl_shm_buffer_get_height(shm) != wantH ||
            (u32)wl_shm_buffer_get_stride(shm) < wantW * 4) {
            captureFail(f, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);

            return;
        }

        size_t stride = (size_t)wl_shm_buffer_get_stride(shm);

        if (cs.cursor) {
            auto* cs2 = (SurfaceImpl*)cur;

            if (!cs2 || cs2->pixels.length() < (size_t)wantW * wantH * 4) {
                captureFail(f, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);

                return;
            }

            wl_shm_buffer_begin_access(shm);

            auto* dst = (unsigned char*)wl_shm_buffer_get_data(shm);

            for (u32 y = 0; y < wantH; y++) {
                memcpy(dst + y * stride, cs2->pixels.data() + (size_t)y * wantW * 4, (size_t)wantW * 4);
            }

            wl_shm_buffer_end_access(shm);
            f.armed = false;
            captureSendReady(f, wantW, wantH);

            return;
        }

        FrameCapture* cap = srv->composer->frameCapture;

        if (!cap) {
            captureFail(f, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);

            return;
        }

        wl_shm_buffer_begin_access(shm);

        bool ok = cap->captureFrame((unsigned char*)wl_shm_buffer_get_data(shm), stride,
                                    0, 0, (int)wantW, (int)wantH);

        wl_shm_buffer_end_access(shm);

        if (ok) {
            f.armed = false;
            captureSendReady(f, wantW, wantH);
        } else if (++f.attempts >= 3) {
            // stays armed once or twice while a direct-scanout frame forces
            // composition; three misses means it is not coming
            captureFail(f, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
        } else {
            srv->scene->needsFrame = true;
        }
    }

    // ---- zwlr-screencopy (compat) ----
    void wlrCopyBufferDestroyed(wl_listener* l, void*) {
        WlrCopyFrame* f = ((WlrCopyBufferDestroyListener*)l)->frame;

        f->buffer = nullptr;
        f->bufferDestroyArmed = false;
        wl_list_remove(&f->bufferDestroy.listener.link);
    }

    void wlrCopyDrop(WlrCopyFrame* f) {
        if (f->bufferDestroyArmed) {
            wl_list_remove(&f->bufferDestroy.listener.link);
            f->bufferDestroyArmed = false;
        }

        f->buffer = nullptr;
    }

    void wlrCopyFrameResourceDestroyed(wl_resource* res) {
        auto* f = (WlrCopyFrame*)wl_resource_get_user_data(res);

        wlrCopyDrop(f);
        removeOne(f->srv->screencopyFrames, f);
        f->srv->alloc->release(f);
    }

    void wlrCopyStart(WlrCopyFrame* f, wl_resource* res, wl_resource* bufferRes, bool withDamage) {
        if (f->used) {
            wl_resource_post_error(res, ZWLR_SCREENCOPY_FRAME_V1_ERROR_ALREADY_USED, "the frame was already used");

            return;
        }

        wl_shm_buffer* shm = wl_shm_buffer_get(bufferRes);

        if (!shm || wl_shm_buffer_get_format(shm) != WL_SHM_FORMAT_XRGB8888 ||
            wl_shm_buffer_get_width(shm) != f->w || wl_shm_buffer_get_height(shm) != f->h ||
            wl_shm_buffer_get_stride(shm) < f->w * 4) {
            wl_resource_post_error(res, ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER, "buffer does not match the announced constraints");

            return;
        }

        f->used = true;
        f->withDamage = withDamage;
        f->buffer = bufferRes;
        f->bufferDestroy.listener.notify = wlrCopyBufferDestroyed;
        f->bufferDestroy.frame = f;
        wl_resource_add_destroy_listener(bufferRes, &f->bufferDestroy.listener);
        f->bufferDestroyArmed = true;
        f->armed = true;
        f->attempts = 0;
        f->srv->scene->needsFrame = true;
    }

    void wlrCopyCopy(wl_client*, wl_resource* res, wl_resource* buffer) {
        wlrCopyStart((WlrCopyFrame*)wl_resource_get_user_data(res), res, buffer, false);
    }

    void wlrCopyCopyWithDamage(wl_client*, wl_resource* res, wl_resource* buffer) {
        wlrCopyStart((WlrCopyFrame*)wl_resource_get_user_data(res), res, buffer, true);
    }

    const struct zwlr_screencopy_frame_v1_interface wlrCopyFrameImpl = {
        .copy = wlrCopyCopy,
        .destroy = resDestroy,
        .copy_with_damage = wlrCopyCopyWithDamage,
    };

    void wlrCopyCapture(WaylandImpl* srv, wl_client* client, wl_resource* managerRes, u32 id,
                        int x, int y, int w, int h) {
        wl_resource* r = wl_resource_create(client, &zwlr_screencopy_frame_v1_interface,
                                            wl_resource_get_version(managerRes), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        // clamp the region to the output; a degenerate region still yields a
        // 1x1 frame rather than a protocol wedge
        RectI rect{x, y, w, h};

        clipRect(rect, srv->scene->outW, srv->scene->outH);

        if (rect.empty()) {
            rect = {0, 0, 1, 1};
        }

        WlrCopyFrame* f = srv->alloc->make<WlrCopyFrame>();

        f->srv = srv;
        f->res = r;
        f->x = rect.x;
        f->y = rect.y;
        f->w = rect.w;
        f->h = rect.h;
        srv->screencopyFrames.pushBack(f);
        wl_resource_set_implementation(r, &wlrCopyFrameImpl, f, wlrCopyFrameResourceDestroyed);

        zwlr_screencopy_frame_v1_send_buffer(r, WL_SHM_FORMAT_XRGB8888, (u32)rect.w, (u32)rect.h, (u32)rect.w * 4);

        if (wl_resource_get_version(r) >= ZWLR_SCREENCOPY_FRAME_V1_BUFFER_DONE_SINCE_VERSION) {
            zwlr_screencopy_frame_v1_send_buffer_done(r);
        }
    }

    void wlrCopyCaptureOutput(wl_client* client, wl_resource* res, u32 id, i32, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);

        wlrCopyCapture(srv, client, res, id, 0, 0, srv->scene->outW, srv->scene->outH);
    }

    void wlrCopyCaptureOutputRegion(wl_client* client, wl_resource* res, u32 id, i32, wl_resource*,
                                    i32 x, i32 y, i32 w, i32 h) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);

        wlrCopyCapture(srv, client, res, id, x, y, w, h);
    }

    const struct zwlr_screencopy_manager_v1_interface wlrCopyManagerImpl = {
        .capture_output = wlrCopyCaptureOutput,
        .capture_output_region = wlrCopyCaptureOutputRegion,
        .destroy = resDestroy,
    };

    void wlrCopyManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &zwlr_screencopy_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &wlrCopyManagerImpl, data, nullptr);
    }

    void fulfillWlrCopy(WaylandImpl* srv, WlrCopyFrame& f) {
        if (!f.buffer) {
            f.armed = false;
            zwlr_screencopy_frame_v1_send_failed(f.res);

            return;
        }

        FrameCapture* cap = srv->composer->frameCapture;
        wl_shm_buffer* shm = wl_shm_buffer_get(f.buffer);

        if (!cap || !shm) {
            f.armed = false;
            zwlr_screencopy_frame_v1_send_failed(f.res);

            return;
        }

        size_t stride = (size_t)wl_shm_buffer_get_stride(shm);

        wl_shm_buffer_begin_access(shm);

        bool ok = cap->captureFrame((unsigned char*)wl_shm_buffer_get_data(shm), stride,
                                    f.x, f.y, f.w, f.h);

        wl_shm_buffer_end_access(shm);

        if (!ok) {
            if (++f.attempts >= 3) {
                f.armed = false;
                zwlr_screencopy_frame_v1_send_failed(f.res);
            } else {
                srv->scene->needsFrame = true;
            }

            return;
        }

        f.armed = false;
        zwlr_screencopy_frame_v1_send_flags(f.res, 0);

        u64 t = nowNs();
        u64 sec = t / 1000000000ull;

        if (f.withDamage) {
            zwlr_screencopy_frame_v1_send_damage(f.res, 0, 0, (u32)f.w, (u32)f.h);
        }

        zwlr_screencopy_frame_v1_send_ready(f.res, (u32)(sec >> 32), (u32)sec, (u32)(t % 1000000000ull));
    }

    // ---- wp-content-type ----
    void contentTypeSetType(wl_client*, wl_resource* res, u32 type) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->pendContentType = type;
            s->pendContentChanged = true;
        }
    }

    void contentTypeResourceDestroyed(wl_resource* res) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->contentTypeRes = nullptr;
            s->pendContentType = 0;
            s->pendContentChanged = true;
        }
    }

    const struct wp_content_type_v1_interface contentTypeImpl = {
        .destroy = resDestroy,
        .set_content_type = contentTypeSetType,
    };

    void contentTypeManagerGetSurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->contentTypeRes) {
            wl_resource_post_error(res, WP_CONTENT_TYPE_MANAGER_V1_ERROR_ALREADY_CONSTRUCTED,
                                   "surface already has a content type object");

            return;
        }

        wl_resource* r = wl_resource_create(client, &wp_content_type_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        s->contentTypeRes = r;
        wl_resource_set_implementation(r, &contentTypeImpl, s, contentTypeResourceDestroyed);
    }

    const struct wp_content_type_manager_v1_interface contentTypeManagerImpl = {
        .destroy = resDestroy,
        .get_surface_content_type = contentTypeManagerGetSurface,
    };

    void contentTypeManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_content_type_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &contentTypeManagerImpl, data, nullptr);
    }

    void contentTypeSurfaceGone(SurfaceImpl& s) {
        if (s.contentTypeRes) {
            wl_resource_set_user_data(s.contentTypeRes, nullptr);
        }
    }

    // ---- xdg-system-bell ----
    void systemBellRing(wl_client*, wl_resource* res, wl_resource*) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);

        // a visible bell: the renderer flashes the screen briefly. The
        // surface argument only scopes which window rang; we flash globally
        srv->scene->bellMs = nowMsec();
        srv->scene->bellCount++;
        srv->scene->needsFrame = true;
    }

    const struct xdg_system_bell_v1_interface systemBellImpl = {
        .destroy = resDestroy,
        .ring = systemBellRing,
    };

    void systemBellBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &xdg_system_bell_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &systemBellImpl, data, nullptr);
    }

    // ---- wp-alpha-modifier ----
    void alphaModSetMultiplier(wl_client*, wl_resource* res, u32 factor) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_ALPHA_MODIFIER_SURFACE_V1_ERROR_NO_SURFACE,
                                   "the surface was destroyed");

            return;
        }

        // wire value: 0 fully transparent, UINT32_MAX fully opaque
        s->pendAlphaMult = (float)((double)factor / (double)UINT32_MAX);
        s->pendAlphaChanged = true;
    }

    void alphaModResourceDestroyed(wl_resource* res) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->alphaModRes = nullptr;
            s->pendAlphaMult = 1.f;
            s->pendAlphaChanged = true;
        }
    }

    const struct wp_alpha_modifier_surface_v1_interface alphaModImpl = {
        .destroy = resDestroy,
        .set_multiplier = alphaModSetMultiplier,
    };

    void alphaModManagerGetSurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->alphaModRes) {
            wl_resource_post_error(res, WP_ALPHA_MODIFIER_V1_ERROR_ALREADY_CONSTRUCTED,
                                   "surface already has an alpha modifier");

            return;
        }

        wl_resource* r = wl_resource_create(client, &wp_alpha_modifier_surface_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        s->alphaModRes = r;
        wl_resource_set_implementation(r, &alphaModImpl, s, alphaModResourceDestroyed);
    }

    const struct wp_alpha_modifier_v1_interface alphaModManagerImpl = {
        .destroy = resDestroy,
        .get_surface = alphaModManagerGetSurface,
    };

    void alphaModManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_alpha_modifier_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &alphaModManagerImpl, data, nullptr);
    }

    void alphaModSurfaceGone(SurfaceImpl& s) {
        if (s.alphaModRes) {
            wl_resource_set_user_data(s.alphaModRes, nullptr);
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
            Buffer name(srv->output ? srv->output->outputName() : "UNKNOWN-1"_sv);

            zxdg_output_v1_send_name(xres, name.cStr());
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
    void gestureResourceDestroyed(Vector<wl_resource*> SeatState::* list,
                                  Vector<wl_resource*> SeatState::* active,
                                  wl_resource* res) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            removeOne(seat->*list, res);
            removeOne(seat->*active, res);
        }
    }

    void swipeResourceDestroyed(wl_resource* res) {
        gestureResourceDestroyed(&SeatState::swipes, &SeatState::activeSwipes, res);
    }

    void pinchResourceDestroyed(wl_resource* res) {
        gestureResourceDestroyed(&SeatState::pinches, &SeatState::activePinches, res);
    }

    void holdResourceDestroyed(wl_resource* res) {
        gestureResourceDestroyed(&SeatState::holds, &SeatState::activeHolds, res);
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
    void regionSnapshot(wl_resource* regionRes, Vector<RectI>& out) {
        out.clear();
        if (auto* rb = (RegionBox*)wl_resource_get_user_data(regionRes)) {
            out.append(rb->rects.begin(), rb->rects.length());
        }
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
            c->srv->scene->confineRegion.clear();
        }

        if (c->surface) {
            c->surface->constraint = nullptr;
        }

        c->srv->alloc->release(c);
    }

    void constraintSetRegion(wl_client*, wl_resource* res, wl_resource* regionRes) {
        auto* c = (ConstraintBox*)wl_resource_get_user_data(res);

        if (!c) {
            return;
        }

        c->pendingSet = true;
        c->pendingHasRegion = regionRes != nullptr;

        if (c->pendingHasRegion) {
            regionSnapshot(regionRes, c->pendingRegion);
        } else {
            c->pendingRegion.clear();
        }
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

        ConstraintBox* c = srv->alloc->make<ConstraintBox>();

        c->srv = srv;
        c->surface = s;
        c->res = r;
        c->isLock = isLock;
        c->oneshot = lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT;
        c->hasRegion = regionRes != nullptr;

        if (c->hasRegion) {
            regionSnapshot(regionRes, c->region);
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
                c->srv->scene->confineRegion.clear();
            }

            c->surface = nullptr;
            s.constraint = nullptr;
        }
    }

    void constraintApplyPending(SurfaceImpl& s) {
        ConstraintBox* c = s.constraint;

        if (!c) {
            return;
        }

        if (c->pendingSet) {
            c->hasRegion = c->pendingHasRegion;
            c->region.xchg(c->pendingRegion);
            c->pendingRegion.clear();
            c->pendingSet = false;
        }

        SeatState& seat = c->srv->seat;

        if (seat.activeConstraint == c && !c->isLock && !seat.updateConfineRegion()) {
            seat.constraintDeactivate();
        }
    }

    // ---- keyboard-shortcuts-inhibit ----
    void kbInhibitorResourceDestroyed(wl_resource* res) {
        if (SurfaceImpl* s = surfaceFrom(res)) {
            s->kbInhibitRes = nullptr;
            s->kbInhibitActive = false;
            s->srv->seat.updateShortcutInhibit();
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
        s->kbInhibitActive = false;
        wl_resource_set_implementation(r, &kbInhibitorImpl, s, kbInhibitorResourceDestroyed);
        s->srv->seat.updateShortcutInhibit();
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
            s.kbInhibitRes = nullptr;
            s.kbInhibitActive = false;
            s.srv->seat.updateShortcutInhibit();

            return;
        }

        s.kbInhibitActive = false;
    }

    // ---- idle-inhibit ----
    void idleInhibitorResourceDestroyed(wl_resource* res) {
        auto* inhibitor = (IdleInhibitor*)wl_resource_get_user_data(res);

        if (!inhibitor) {
            return;
        }

        inhibitor->unlink();
        inhibitor->srv->alloc->release(inhibitor);
    }

    const struct zwp_idle_inhibitor_v1_interface idleInhibitorImpl = {.destroy = relPointerDestroy};

    void idleInhibitManagerCreateInhibitor(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &zwp_idle_inhibitor_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* inhibitor = srv->alloc->make<IdleInhibitor>();

        inhibitor->srv = srv;
        inhibitor->surface = surfaceFrom(surfaceRes);
        inhibitor->res = r;
        srv->idleInhibitors.pushBack(inhibitor);
        wl_resource_set_implementation(r, &idleInhibitorImpl, inhibitor, idleInhibitorResourceDestroyed);
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

        if (!n->ignoreInhibitors && n->srv->idleBlocked()) {
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
        n->unlink();
        n->srv->alloc->release(n);
    }

    const struct ext_idle_notification_v1_interface idleNotificationImpl = {.destroy = relPointerDestroy};

    void idleMakeNotification(wl_client* client, wl_resource* res, u32 id, u32 timeoutMs, bool ignoreInhibitors) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* r = wl_resource_create(client, &ext_idle_notification_v1_interface, wl_resource_get_version(res), id);

        if (!r) {
            wl_client_post_no_memory(client);

            return;
        }

        WaylandImpl::IdleNotif* n = srv->alloc->make<WaylandImpl::IdleNotif>();

        n->srv = srv;
        n->res = r;
        n->ignoreInhibitors = ignoreInhibitors;

        double t = timeoutMs > 0 ? timeoutMs / 1000.0 : 0.001;

        ev_timer_init(&n->timer, idleNotifCb, t, t);
        n->timer.data = n;
        ev_timer_again(srv->loop, &n->timer);

        srv->idleNotifs.pushBack(n);
        wl_resource_set_implementation(r, &idleNotificationImpl, n, idleNotificationResourceDestroyed);
    }

    void idleNotifierGetNotification(wl_client* client, wl_resource* res, u32 id, u32 timeoutMs, wl_resource*) {
        idleMakeNotification(client, res, id, timeoutMs, false);
    }

    void idleNotifierGetInputNotification(wl_client* client, wl_resource* res, u32 id, u32 timeoutMs, wl_resource*) {
        idleMakeNotification(client, res, id, timeoutMs, true);
    }

    const struct ext_idle_notifier_v1_interface idleNotifierImpl = {
        .destroy = relPointerDestroy,
        .get_idle_notification = idleNotifierGetNotification,
        .get_input_idle_notification = idleNotifierGetInputNotification,
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
        tlUnref((TimelineBox*)wl_resource_get_user_data(res));
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

        tlRef(t);
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

        tlRef(t);
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

        FrameResource* lifetime = frameCreate();
        TimelineBox* t = lifetime->make<TimelineBox>();

        t->srv = srv;
        t->lifetime = lifetime;
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

        if (srv->idleBlocked()) {
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

        box->srv->alloc->release(box);
    }

    void spbCreateBuffer(wl_client* client, wl_resource* res, u32 id, u32 r, u32 g, u32 b, u32 a) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* buf = wl_resource_create(client, &wl_buffer_interface, 1, id);

        if (!buf) {
            wl_client_post_no_memory(client);

            return;
        }

        SpbBox* box = srv->alloc->make<SpbBox>();

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
    void iconBufferDestroyed(wl_listener* listener, void*) {
        auto* destroy = (IconBufferDestroyListener*)listener;
        IconBufferWatch* watch = destroy->watch;
        IconBox* box = watch->icon;

        wl_list_remove(&destroy->listener.link);
        watch->unlink();
        wl_resource_post_error(box->res, XDG_TOPLEVEL_ICON_V1_ERROR_NO_BUFFER,
                               "icon buffer was destroyed before the icon object");
        box->srv->alloc->release(watch);
    }

    void iconResourceDestroyed(wl_resource* res) {
        auto* box = (IconBox*)wl_resource_get_user_data(res);

        while (!box->bufferWatches.empty()) {
            auto* watch = (IconBufferWatch*)box->bufferWatches.popFront();

            wl_list_remove(&watch->destroy.listener.link);
            box->srv->alloc->release(watch);
        }

        box->srv->alloc->release(box);
    }

    void iconSetName(wl_client*, wl_resource* res, const char* name) {
        auto* box = (IconBox*)wl_resource_get_user_data(res);

        if (box->immutable) {
            wl_resource_post_error(res, XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE,
                                   "icon was already assigned to a toplevel");

            return;
        }

        box->name.reset();
        box->name << name;
    }

    void iconAddBuffer(wl_client*, wl_resource* res, wl_resource* bufferRes, i32 scale) {
        auto* box = (IconBox*)wl_resource_get_user_data(res);

        if (box->immutable) {
            wl_resource_post_error(res, XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE,
                                   "icon was already assigned to a toplevel");

            return;
        }

        wl_shm_buffer* shm = wl_shm_buffer_get(bufferRes);

        if (!shm) {
            wl_resource_post_error(res, XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER,
                                   "icon buffer must be backed by wl_shm");

            return;
        }

        u32 fmt = wl_shm_buffer_get_format(shm);

        if (fmt != WL_SHM_FORMAT_ARGB8888 && fmt != WL_SHM_FORMAT_XRGB8888) {
            wl_resource_post_error(res, XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER,
                                   "unsupported icon buffer format");

            return;
        }

        int w = wl_shm_buffer_get_width(shm);
        int h = wl_shm_buffer_get_height(shm);

        if (w <= 0 || h <= 0 || w != h || scale <= 0) {
            wl_resource_post_error(res, XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER,
                                   "icon buffer must be square with a positive scale");

            return;
        }

        i32 stride = wl_shm_buffer_get_stride(shm);

        if (stride < (i64)w * 4) {
            wl_resource_post_error(res, XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER,
                                   "icon buffer stride is too small");

            return;
        }

        if (w > kIconMaxSize || box->bufferWatchCount >= kIconMaxBuffers) {
            sysO << "imway: ignoring toplevel icon buffer ("_sv << w << "x"_sv << h << ", "_sv << box->bufferWatchCount << " buffers already)"_sv << endL;

            return;
        }

        box->bufferWatchCount++;

        IconBufferWatch* watch = box->srv->alloc->make<IconBufferWatch>();

        watch->icon = box;
        watch->destroy.watch = watch;
        watch->destroy.listener.notify = iconBufferDestroyed;
        box->bufferWatches.pushBack(watch);
        wl_resource_add_destroy_listener(bufferRes, &watch->destroy.listener);

        int effectiveSize = w / scale;

        if (effectiveSize < box->effectiveSize) {
            return;
        }

        wl_shm_buffer_begin_access(shm);

        const u8* src = (const u8*)wl_shm_buffer_get_data(shm);

        box->pixels.clear();

        for (int y = 0; y < h; y++) {
            box->pixels.append((const u32*)(src + (size_t)y * stride), (size_t)w);
        }

        wl_shm_buffer_end_access(shm);

        // icons render as ARGB; XRGB leaves the alpha byte undefined
        if (fmt == WL_SHM_FORMAT_XRGB8888) {
            for (size_t i = 0; i < box->pixels.length(); i++) {
                box->pixels.mut(i) |= 0xff000000u;
            }
        }

        box->w = w;
        box->h = h;
        box->effectiveSize = effectiveSize;
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

        IconBox* box = srv->alloc->make<IconBox>();

        box->srv = srv;
        box->res = r;
        wl_resource_set_implementation(r, &iconImpl, box, iconResourceDestroyed);
    }

    void iconManagerSetIcon(wl_client*, wl_resource* res, wl_resource* toplevelRes, wl_resource* iconRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(toplevelRes);

        if (!t) {
            return;
        }

        if (t->pendingOwnIcon && srv->iconPool) {
            srv->iconPool->release(t->pendingOwnIcon);
        }

        t->pendingOwnIcon = nullptr;
        t->pendingIcon = nullptr;
        t->pendingIconFromClient = false;
        t->pendingIconSet = true;

        if (iconRes) {
            auto* box = (IconBox*)wl_resource_get_user_data(iconRes);

            box->immutable = true;

            if (box->pixels.length() && srv->iconPool) {
                Icon* ic = srv->iconPool->acquire();

                ic->width = box->w;
                ic->height = box->h;
                ic->argb.append(box->pixels.data(), box->pixels.length());
                t->pendingOwnIcon = ic;
                t->pendingIcon = ic;
                t->pendingIconFromClient = true;
            } else if (!box->name.empty() && srv->icons) {
                t->pendingIcon = srv->icons->byName(sv(box->name));
                t->pendingIconFromClient = t->pendingIcon != nullptr;
            }
        }

        if (!t->pendingIconFromClient && srv->icons) {
            // back to the .desktop match
            t->pendingIcon = srv->icons->forAppId(sv(t->appId));
        }
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

    bool activationTokenMutable(wl_resource* res, ActivationTokenRequest* request) {
        if (request->committed) {
            wl_resource_post_error(res, XDG_ACTIVATION_TOKEN_V1_ERROR_ALREADY_USED, "activation token was already committed");

            return false;
        }

        return true;
    }

    void activationTokenSetSerial(wl_client*, wl_resource* res, u32 serial, wl_resource* seatRes) {
        auto* request = (ActivationTokenRequest*)wl_resource_get_user_data(res);

        if (!activationTokenMutable(res, request)) {
            return;
        }

        auto* seat = (SeatState*)wl_resource_get_user_data(seatRes);

        if (seat == &request->srv->seat) {
            request->serial = serial;
            request->serialSet = true;
        }
    }

    void activationTokenSetAppId(wl_client*, wl_resource* res, const char* appId) {
        auto* request = (ActivationTokenRequest*)wl_resource_get_user_data(res);

        if (!activationTokenMutable(res, request)) {
            return;
        }

        request->appId.reset();
        request->appId << appId;
    }

    void activationTokenSetSurface(wl_client*, wl_resource* res, wl_resource* surfaceRes) {
        auto* request = (ActivationTokenRequest*)wl_resource_get_user_data(res);

        if (activationTokenMutable(res, request)) {
            request->surface = surfaceFrom(surfaceRes);
            request->surfaceSet = true;
        }
    }

    void activationTokenCommit(wl_client*, wl_resource* res) {
        auto* request = (ActivationTokenRequest*)wl_resource_get_user_data(res);

        if (!activationTokenMutable(res, request)) {
            return;
        }

        request->committed = true;
        ActivationGrant grant;
        u64 random = 0;

        if (getrandom(&random, sizeof(random), GRND_NONBLOCK) != sizeof(random)) {
            random = ((u64)nowMsec() << 32) ^ ++request->srv->tokenCounter;
        }

        snprintf(grant.token, sizeof(grant.token), "imway-%016llx-%016llx",
                 (unsigned long long)++request->srv->tokenCounter, (unsigned long long)random);
        grant.authorized = request->serialSet && request->srv->seat.validSerial(request->client, request->serial) &&
                           (!request->surfaceSet || (request->surface && wl_resource_get_client(resOf(request->surface)) == request->client));

        if (request->srv->activationGrants.length() == 64) {
            for (size_t i = 1; i < request->srv->activationGrants.length(); i++) {
                request->srv->activationGrants.mut(i - 1) = request->srv->activationGrants[i];
            }

            request->srv->activationGrants.popBack();
        }

        request->srv->activationGrants.pushBack(grant);
        xdg_activation_token_v1_send_done(res, grant.token);
    }

    const struct xdg_activation_token_v1_interface activationTokenImpl = {
        .set_serial = activationTokenSetSerial,
        .set_app_id = activationTokenSetAppId,
        .set_surface = activationTokenSetSurface,
        .commit = activationTokenCommit,
        .destroy = resDestroy,
    };

    void activationTokenResourceDestroyed(wl_resource* res) {
        auto* request = (ActivationTokenRequest*)wl_resource_get_user_data(res);

        request->unlink();
        request->srv->alloc->release(request);
    }

    void activationGetToken(wl_client* client, wl_resource* res, u32 id) {
        wl_resource* t = wl_resource_create(client, &xdg_activation_token_v1_interface, 1, id);

        if (!t) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        auto* request = srv->alloc->make<ActivationTokenRequest>();

        request->srv = srv;
        request->client = client;
        srv->activationTokenRequests.pushBack(request);
        wl_resource_set_implementation(t, &activationTokenImpl, request, activationTokenResourceDestroyed);
    }

    void activationActivate(wl_client*, wl_resource* res, const char* token, wl_resource* surfRes) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        SurfaceImpl* s = surfaceFrom(surfRes);
        Toplevel* tl = s ? s->rootToplevel() : nullptr;
        bool authorized = false;

        for (size_t i = 0; i < srv->activationGrants.length(); i++) {
            if (strcmp(srv->activationGrants[i].token, token) == 0) {
                authorized = srv->activationGrants[i].authorized;
                srv->activationGrants.mut(i) = srv->activationGrants.back();
                srv->activationGrants.popBack();

                break;
            }
        }

        if (!authorized || !tl || !tl->mapped) {
            return;
        }

        sysO << "imway: activation ("_sv << token << ") -> "_sv << sv(tl->title) << endL;
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

        return ((BufferBox*)wl_resource_get_user_data(res))->buf;
    }

    void dmabufBufferResourceDestroyed(wl_resource* res) {
        auto* box = (BufferBox*)wl_resource_get_user_data(res);

        box->srv->alloc->release(box);
    }

    Params* paramsFrom(wl_resource* res) {
        return (Params*)wl_resource_get_user_data(res);
    }

    void paramsDropPending(Params& p) {
        BufferBox* box = p.pending;

        p.pending = nullptr;
        p.srv->alloc->release(box);
    }

    bool dmabufFdsImportable(WaylandImpl& srv, const DmabufBuffer& b) {
        if (srv.drmFd < 0) {
            return true;
        }

        for (int i = 0; i < b.nplanes; i++) {
            u32 handle = 0;

            if (drmPrimeFDToHandle(srv.drmFd, b.fds[i], &handle) != 0) {
                return false;
            }

            drm_gem_close gc{};

            gc.handle = handle;
            drmIoctl(srv.drmFd, DRM_IOCTL_GEM_CLOSE, &gc);
        }

        return true;
    }

    void paramsDestroyResource(wl_resource* res) {
        Params* p = paramsFrom(res);

        if (p->pending) {
            p->srv->alloc->release(p->pending);
        }

        p->srv->alloc->release(p);
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

        DmabufBuffer& b = *p->pending->buf;

        if (b.fds[planeIdx] >= 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET, "plane %u already set", planeIdx);
            close(fd);

            return;
        }

        u64 modifier = ((u64)modifierHi << 32) | modifierLo;

        for (int i = 0; i < kDmabufMaxPlanes; i++) {
            if (b.fds[i] >= 0 && b.modifier != modifier) {
                wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "all planes must use the same modifier");
                close(fd);

                return;
            }
        }

        if (stride == 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS, "plane stride must be nonzero");
            close(fd);

            return;
        }

        b.fds[planeIdx] = fd;
        b.offsets[planeIdx] = offset;
        b.strides[planeIdx] = stride;
        b.modifier = modifier;

        if ((int)planeIdx + 1 > b.nplanes) {
            b.nplanes = (int)planeIdx + 1;
        }
    }

    wl_resource* paramsMakeBuffer(wl_client* client, wl_resource* res, u32 bufferId,
                                  i32 width, i32 height, u32 format, u32 flags,
                                  bool immediate) {
        Params* p = paramsFrom(res);

        if (!p->pending) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED, "params already used");

            return nullptr;
        }

        if (width <= 0 || height <= 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS, "invalid dimensions %dx%d", width, height);

            return nullptr;
        }

        DmabufBuffer& b = *p->pending->buf;

        if (b.nplanes == 0 || b.fds[0] < 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "plane 0 missing");

            return nullptr;
        }

        for (int i = 0; i < b.nplanes; i++) {
            if (b.fds[i] < 0) {
                wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "plane %d missing", i);

                return nullptr;
            }

            u64 end = (u64)b.offsets[i] + (u64)b.strides[i] * (u64)height;

            if (end > 0xffffffffull) {
                wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS, "plane %d offset/stride overflows", i);

                return nullptr;
            }
        }

        if (!p->srv->formatSupported(format, b.modifier)) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, "format 0x%x not supported", format);

            return nullptr;
        }

        int expectedPlanes = format == kFourccNv12 || format == kFourccP010 ? 2 : 1;

        if (b.nplanes != expectedPlanes) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                                   "format 0x%x requires exactly %d plane(s)",
                                   format, expectedPlanes);

            return nullptr;
        }

        if (flags != 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                                   "interlaced and y-inverted buffers are unsupported");

            return nullptr;
        }

        if (!dmabufFdsImportable(*p->srv, b)) {
            paramsDropPending(*p);

            if (immediate) {
                wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
                                       "fd cannot be imported as a dmabuf");
            }

            return nullptr;
        }

        wl_resource* bres = wl_resource_create(client, &wl_buffer_interface, 1, bufferId);

        if (!bres) {
            wl_client_post_no_memory(client);

            return nullptr;
        }

        BufferBox* box = p->pending;

        p->pending = nullptr;
        box->buf->width = width;
        box->buf->height = height;
        box->buf->format = format;
        wl_resource_set_implementation(bres, &dmabufWlBufferImpl, box, dmabufBufferResourceDestroyed);

        return bres;
    }

    void paramsCreate(wl_client* client, wl_resource* res, i32 width, i32 height, u32 format, u32 flags) {
        wl_resource* buf = paramsMakeBuffer(client, res, 0, width, height, format, flags, false);

        if (buf) {
            zwp_linux_buffer_params_v1_send_created(res, buf);
        } else if (!paramsFrom(res)->pending) {
            zwp_linux_buffer_params_v1_send_failed(res);
        }
    }

    void paramsCreateImmed(wl_client* client, wl_resource* res, u32 bufferId, i32 width, i32 height, u32 format, u32 flags) {
        paramsMakeBuffer(client, res, bufferId, width, height, format, flags, true);
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

        auto* p = srv->alloc->make<Params>();

        p->srv = srv;
        p->pending = srv->alloc->make<BufferBox>();
        p->pending->srv = srv;
        FrameResource* lifetime = frameCreate();

        p->pending->buf = lifetime->make<DmabufBuffer>();
        p->pending->buf->lifetime = lifetime;
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

        // preference order: the scanout-capable subset first, so fullscreen
        // clients allocate buffers eligible for the direct scanout bypass
        if (!srv->scanoutIndices.empty()) {
            wl_array scanout;

            wl_array_init(&scanout);

            for (u16 index : srv->scanoutIndices) {
                *(u16*)wl_array_add(&scanout, sizeof(u16)) = index;
            }

            zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(res, &dev);
            zwp_linux_dmabuf_feedback_v1_send_tranche_formats(res, &scanout);
            wl_array_release(&scanout);
            zwp_linux_dmabuf_feedback_v1_send_tranche_flags(
                res, ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT);
            zwp_linux_dmabuf_feedback_v1_send_tranche_done(res);
        }

        wl_array indices;

        wl_array_init(&indices);

        for (u16 i = 0; i < (u16)srv->formats.length(); i++) {
            *(u16*)wl_array_add(&indices, sizeof(u16)) = i;
        }

        zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(res, &dev);
        zwp_linux_dmabuf_feedback_v1_send_tranche_formats(res, &indices);
        wl_array_release(&indices);
        zwp_linux_dmabuf_feedback_v1_send_tranche_flags(res, 0);
        zwp_linux_dmabuf_feedback_v1_send_tranche_done(res);
        zwp_linux_dmabuf_feedback_v1_send_done(res);
        wl_array_release(&dev);
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
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_RESIZE:
                return CursorKind::move;
            case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DND_ASK:
                return CursorKind::hand;
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

    void cursorShapeDeviceSetShape(wl_client* client, wl_resource* res, u32 serial, u32 shape) {
        auto* seat = (SeatState*)wl_resource_get_user_data(res);

        if (!wp_cursor_shape_device_v1_shape_is_valid(shape, wl_resource_get_version(res))) {
            wl_resource_post_error(res, WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE,
                                   "invalid cursor shape");

            return;
        }

        if (!seat || !seat->ptrFocus || !seat->validCursorShapeEnter(res, serial)) {
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

    void cursorShapeDeviceDestroyed(wl_resource* res) {
        if (auto* seat = (SeatState*)wl_resource_get_user_data(res)) {
            seat->forgetCursorShapeDevice(res);
        }
    }

    void cursorShapeGetPointer(wl_client* client, wl_resource* res, u32 id, wl_resource* pointerRes) {
        wl_resource* d = wl_resource_create(client, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(res), id);

        if (!d) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* seat = (SeatState*)wl_resource_get_user_data(pointerRes);

        wl_resource_set_implementation(d, &cursorShapeDeviceImpl, seat, cursorShapeDeviceDestroyed);
        seat->cursorShapeBindings.pushBack({d, pointerRes});
    }

    void cursorShapeGetTabletTool(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        wl_resource* d = wl_resource_create(client, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(res), id);

        if (d) {
            wl_resource_set_implementation(d, &cursorShapeDeviceImpl, nullptr, cursorShapeDeviceDestroyed);
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

    constexpr u32 kSeatVersion = 11;

    SeatState* seatOf(wl_resource* res) {
        return (SeatState*)wl_resource_get_user_data(res);
    }

    void pointerSetCursor(wl_client* client, wl_resource* res, u32 serial, wl_resource* surfRes, i32 hotX, i32 hotY) {
        SeatState* seat = seatOf(res);

        if (!seat || !seat->ptrFocus || !seat->validPointerEnter(res, serial)) {
            return;
        }

        if (wl_resource_get_client(resOf(seat->ptrFocus)) != client) {
            return;
        }

        SurfaceImpl* cursor = surfRes ? surfaceFrom(surfRes) : nullptr;

        if (cursor && cursor->role != SurfaceRole::none && cursor->role != SurfaceRole::cursor) {
            wl_resource_post_error(res, WL_POINTER_ERROR_ROLE, "cursor surface already has another role");

            return;
        }

        if (cursor) {
            cursor->role = SurfaceRole::cursor;
        }

        Scene* scn = seat->srv->scene;

        scn->cursorSurface = cursor;
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
        SeatState* seat = seatOf(res);

        removeOne(seat->pointers, res);
        seat->forgetPointer(res);
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

        // a pointer created while its surface already holds focus gets the
        // enter right away — the keyboard path below does the same
        SeatState& st = *seat;

        if (st.ptrFocus && wl_resource_get_client(resOf(st.ptrFocus)) == client) {
            u32 serial = wl_display_next_serial(st.srv->display);

            st.rememberSerial(serial, client, st.ptrFocus);
            st.rememberPointerEnter(p, serial);
            wl_pointer_send_enter(p, serial, resOf(st.ptrFocus), wl_fixed_from_double(st.curX - st.ptrFocus->imgX), wl_fixed_from_double(st.curY - st.ptrFocus->imgY));

            if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                wl_pointer_send_frame(p);
            }
        }
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

            u32 serial = wl_display_next_serial(s.srv->display);

            s.rememberSerial(serial, client, s.kbFocus->surface);
            wl_keyboard_send_enter(k, serial, resOf(s.kbFocus->surface), &keys);
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

static u32 flipPositionerX(u32 value) {
    switch (value) {
        case XDG_POSITIONER_ANCHOR_LEFT: return XDG_POSITIONER_ANCHOR_RIGHT;
        case XDG_POSITIONER_ANCHOR_RIGHT: return XDG_POSITIONER_ANCHOR_LEFT;
        case XDG_POSITIONER_ANCHOR_TOP_LEFT: return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
        case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT: return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
        case XDG_POSITIONER_ANCHOR_TOP_RIGHT: return XDG_POSITIONER_ANCHOR_TOP_LEFT;
        case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT: return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
        default: return value;
    }
}

static u32 flipPositionerY(u32 value) {
    switch (value) {
        case XDG_POSITIONER_ANCHOR_TOP: return XDG_POSITIONER_ANCHOR_BOTTOM;
        case XDG_POSITIONER_ANCHOR_BOTTOM: return XDG_POSITIONER_ANCHOR_TOP;
        case XDG_POSITIONER_ANCHOR_TOP_LEFT: return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
        case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT: return XDG_POSITIONER_ANCHOR_TOP_LEFT;
        case XDG_POSITIONER_ANCHOR_TOP_RIGHT: return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
        case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT: return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
        default: return value;
    }
}

static void placePositionerRaw(const Positioner& p, u32 anchor, u32 gravity, int& outX, int& outY) {
    i64 px = p.ax, py = p.ay;

    switch (anchor) {
        case XDG_POSITIONER_ANCHOR_TOP:
            px += p.aw / 2;
            break;
        case XDG_POSITIONER_ANCHOR_BOTTOM:
            px += p.aw / 2;
            py += p.ah;
            break;
        case XDG_POSITIONER_ANCHOR_LEFT:
            py += p.ah / 2;
            break;
        case XDG_POSITIONER_ANCHOR_RIGHT:
            px += p.aw;
            py += p.ah / 2;
            break;
        case XDG_POSITIONER_ANCHOR_TOP_LEFT:
            break;
        case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
            py += p.ah;
            break;
        case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
            px += p.aw;
            break;
        case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
            px += p.aw;
            py += p.ah;
            break;
        default:
            px += p.aw / 2;
            py += p.ah / 2;
            break;
    }

    switch (gravity) {
        case XDG_POSITIONER_GRAVITY_TOP:
            px -= p.w / 2;
            py -= p.h;
            break;
        case XDG_POSITIONER_GRAVITY_BOTTOM:
            px -= p.w / 2;
            break;
        case XDG_POSITIONER_GRAVITY_LEFT:
            px -= p.w;
            py -= p.h / 2;
            break;
        case XDG_POSITIONER_GRAVITY_RIGHT:
            py -= p.h / 2;
            break;
        case XDG_POSITIONER_GRAVITY_TOP_LEFT:
            px -= p.w;
            py -= p.h;
            break;
        case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
            px -= p.w;
            break;
        case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
            py -= p.h;
            break;
        case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
            break;
        default:
            px -= p.w / 2;
            py -= p.h / 2;
            break;
    }

    outX = clampPosition(px + p.dx);
    outY = clampPosition(py + p.dy);
}

void Positioner::place(int& outX, int& outY, int& outW, int& outH,
                       int minX, int minY, int maxX, int maxY) const {
    placePositionerRaw(*this, anchor, gravity, outX, outY);
    outW = w;
    outH = h;

    bool badX = outX < minX || (i64)outX + outW > maxX;
    bool badY = outY < minY || (i64)outY + outH > maxY;

    if (badX && (constraints & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X)) {
        int flippedX = 0, ignoredY = 0;

        placePositionerRaw(*this, flipPositionerX(anchor), flipPositionerX(gravity), flippedX, ignoredY);

        if (flippedX >= minX && (i64)flippedX + outW <= maxX) {
            outX = flippedX;
            badX = false;
        }
    }

    if (badY && (constraints & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y)) {
        int ignoredX = 0, flippedY = 0;

        placePositionerRaw(*this, flipPositionerY(anchor), flipPositionerY(gravity), ignoredX, flippedY);

        if (flippedY >= minY && (i64)flippedY + outH <= maxY) {
            outY = flippedY;
            badY = false;
        }
    }

    if (badX && (constraints & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X) && outW <= maxX - minX) {
        if (outX < minX) {
            outX = minX;
        } else if ((i64)outX + outW > maxX) {
            outX = maxX - outW;
        }

        badX = false;
    }

    if (badY && (constraints & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y) && outH <= maxY - minY) {
        if (outY < minY) {
            outY = minY;
        } else if ((i64)outY + outH > maxY) {
            outY = maxY - outH;
        }

        badY = false;
    }

    if (badX && (constraints & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X) && maxX > minX) {
        i64 left = outX > minX ? outX : minX;
        i64 right = (i64)outX + outW < maxX ? (i64)outX + outW : maxX;

        if (right <= left) {
            left = minX;
            right = maxX;
        }

        outX = (int)left;
        outW = (int)(right - left);
    }

    if (badY && (constraints & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y) && maxY > minY) {
        i64 top = outY > minY ? outY : minY;
        i64 bottom = (i64)outY + outH < maxY ? (i64)outY + outH : maxY;

        if (bottom <= top) {
            top = minY;
            bottom = maxY;
        }

        outY = (int)top;
        outH = (int)(bottom - top);
    }
}

SeatState::SeatState(WaylandImpl& impl)
    : srv(&impl)
{
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

    forEach<Subsurface>(s.stackBelow, [&](Subsurface& c) {
        if (c.surface && c.surface->hasContent) {
            if (Surface* f = pickInTree(*c.surface)) {
                found = f;
            }
        }
    });

    if (s.hovered && s.inputContains(curX - s.imgX, curY - s.imgY)) {
        found = &s;
    }

    forEach<Subsurface>(s.stackAbove, [&](Subsurface& c) {
        if (c.surface && c.surface->hasContent) {
            if (Surface* f = pickInTree(*c.surface)) {
                found = f;
            }
        }
    });

    return found;
}

Surface* SeatState::pickPointerTarget() {
    for (Popup* p : eachRev<Popup>(srv->scene->popups)) {
        if (!p->mapped || !p->surface) {
            continue;
        }

        if (Surface* s = pickInTree(*p->surface)) {
            return s;
        }
    }

    for (Toplevel* t : each<Toplevel>(srv->scene->toplevels)) {
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
    pointerEnters.clear();
    srv->scene->cursorShape = CursorKind::unset;
    srv->scene->cursorSurface = nullptr;

    if (s) {
        u32 serial = wl_display_next_serial(srv->display);
        wl_client* client = wl_resource_get_client(resOf(s));
        bool delivered = false;

        for (wl_resource* p : pointers) {
            if (sameClientS(p, s)) {
                wl_pointer_send_enter(p, serial, resOf(s), wl_fixed_from_double(sx), wl_fixed_from_double(sy));
                rememberPointerEnter(p, serial);
                delivered = true;

                if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                    wl_pointer_send_frame(p);
                }
            }
        }

        if (delivered) {
            rememberSerial(serial, client, s);
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
        if (!updateConfineRegion()) {
            activeConstraint = nullptr;
            srv->scene->pointerConfined = false;

            return;
        }

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
    srv->scene->confineRegion.clear();

    if (c->isLock) {
        zwp_locked_pointer_v1_send_unlocked(c->res);
    } else {
        zwp_confined_pointer_v1_send_unconfined(c->res);
    }

    if (c->oneshot) {
        c->dead = true;
    }
}

bool SeatState::updateConfineRegion() {
    ConstraintBox* c = activeConstraint;

    if (!c || c->isLock || !ptrFocus) {
        return false;
    }

    Scene* scn = srv->scene;
    Vector<RectI> regions;

    regions.pushBack({ptrFocus->geomX(), ptrFocus->geomY(), ptrFocus->geomW(), ptrFocus->geomH()});

    auto intersect = [&](const Vector<RectI>& mask) {
        Vector<RectI> next;

        for (const RectI& a : regions) {
            for (const RectI& b : mask) {
                i64 x0 = a.x > b.x ? a.x : b.x;
                i64 y0 = a.y > b.y ? a.y : b.y;
                i64 ax1 = (i64)a.x + a.w, ay1 = (i64)a.y + a.h;
                i64 bx1 = (i64)b.x + b.w, by1 = (i64)b.y + b.h;
                i64 x1 = ax1 < bx1 ? ax1 : bx1;
                i64 y1 = ay1 < by1 ? ay1 : by1;

                if (x1 > x0 && y1 > y0) {
                    next.pushBack({(i32)x0, (i32)y0, (i32)(x1 - x0), (i32)(y1 - y0)});
                }
            }
        }

        regions.xchg(next);
    };

    if (c->hasRegion) {
        intersect(c->region);
    }

    if (ptrFocus->inputRegionSet) {
        intersect(ptrFocus->inputRegion);
    }

    scn->confineRegion.clear();

    for (RectI r : regions) {
        r.x += (i32)ptrFocus->imgX;
        r.y += (i32)ptrFocus->imgY;
        clipRect(r, scn->outW, scn->outH);

        if (!r.empty()) {
            scn->confineRegion.pushBack(r);
        }
    }

    return !scn->confineRegion.empty();
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

    if (!activeSwipes.empty()) {
        handleSwipeEnd(true);
    }

    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : swipes) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_swipe_v1_send_begin(r, serial, t, resOf(ptrFocus), fingers);
            activeSwipes.pushBack(r);
        }
    }
}

void SeatState::handleSwipeUpdate(double dx, double dy) {
    u32 t = nowMsec();

    for (wl_resource* r : activeSwipes) {
        zwp_pointer_gesture_swipe_v1_send_update(r, t, wl_fixed_from_double(dx), wl_fixed_from_double(dy));
    }
}

void SeatState::handleSwipeEnd(bool cancelled) {
    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : activeSwipes) {
        zwp_pointer_gesture_swipe_v1_send_end(r, serial, t, cancelled);
    }

    activeSwipes.clear();
}

void SeatState::handlePinchBegin(u32 fingers) {
    if (!ptrFocus) {
        return;
    }

    if (!activePinches.empty()) {
        handlePinchEnd(true);
    }

    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : pinches) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_pinch_v1_send_begin(r, serial, t, resOf(ptrFocus), fingers);
            activePinches.pushBack(r);
        }
    }
}

void SeatState::handlePinchUpdate(double dx, double dy, double scale, double rotation) {
    u32 t = nowMsec();

    for (wl_resource* r : activePinches) {
        zwp_pointer_gesture_pinch_v1_send_update(r, t, wl_fixed_from_double(dx), wl_fixed_from_double(dy), wl_fixed_from_double(scale), wl_fixed_from_double(rotation));
    }
}

void SeatState::handlePinchEnd(bool cancelled) {
    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : activePinches) {
        zwp_pointer_gesture_pinch_v1_send_end(r, serial, t, cancelled);
    }

    activePinches.clear();
}

void SeatState::handleHoldBegin(u32 fingers) {
    if (!ptrFocus) {
        return;
    }

    if (!activeHolds.empty()) {
        handleHoldEnd(true);
    }

    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : holds) {
        if (sameClientS(r, ptrFocus)) {
            zwp_pointer_gesture_hold_v1_send_begin(r, serial, t, resOf(ptrFocus), fingers);
            activeHolds.pushBack(r);
        }
    }
}

void SeatState::handleHoldEnd(bool cancelled) {
    u32 serial = wl_display_next_serial(srv->display), t = nowMsec();

    for (wl_resource* r : activeHolds) {
        zwp_pointer_gesture_hold_v1_send_end(r, serial, t, cancelled);
    }

    activeHolds.clear();
}

void WaylandImpl::activity() {
    forEach<IdleNotif>(idleNotifs, [&](IdleNotif& n) {
        if (n.idled) {
            n.idled = false;
            ext_idle_notification_v1_send_resumed(n.res);
        }

        ev_timer_again(loop, &n.timer);
    });

    if (dpmsSec > 0 && output) {
        ev_timer_again(loop, &dpmsTimer);

        if (dpmsOff) {
            dpmsOff = false;
            output->setPowerSave(true);
            scene->needsFrame = true;
        }
    }
}

TimelineBox::~TimelineBox() noexcept {
    if (handle) {
        drmSyncobjDestroy(srv->drmFd, handle);
    }
}

DmabufUse::~DmabufUse() noexcept {
    if (rel) {
        drmSyncobjTimelineSignal(srv->drmFd, &rel->handle, &relPoint, 1);
    }

    tlUnref(acq);
    tlUnref(rel);

    if (res) {
        wl_buffer_send_release(res);
        wl_list_remove(&destroy.listener.link);
    }

    // v7 get_release fires with the buffer's release
    if (releaseCb) {
        wl_callback_send_done(releaseCb, 0);
        wl_resource_destroy(releaseCb);
        releaseCb = nullptr;
    }

    dmabufUnref(buffer);
}

bool WaylandImpl::idleBlocked() {
    for (IdleInhibitor* inhibitor : each<IdleInhibitor>(idleInhibitors)) {
        Surface* surface = inhibitor->surface;

        if (!surface || !surface->hasContent) {
            continue;
        }

        Surface* root = surface->rootSurface();

        if (root->toplevel && root->toplevel->mapped) {
            return true;
        }

        for (Popup* popup : each<Popup>(scene->popups)) {
            if (popup->surface == root && popup->mapped) {
                return true;
            }
        }
    }

    return false;
}

void SeatState::handleMotion(double x, double y) {
    curX = x;
    curY = y;
    srv->scene->needsFrame = true;

    if (dragClient) {
        dragMotion();

        return;
    }

    // pointer is over the compositor's own ui: the client sees a leave —
    // unless a button we delivered is still held. imgui keeps WantCaptureMouse
    // for the whole press-drag it saw start, so a drag leaving the window
    // would read as "captured" and break the implicit grab: the client must
    // keep its motion stream and the matching release
    if (srv->scene->ptrCaptured && buttonsDown == 0) {
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

    if (activeConstraint && !activeConstraint->isLock) {
        updateConfineRegion();
    }

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

    if (dragClient) {
        buttonsDown += pressed ? 1 : -1;

        if (!pressed && buttonsDown <= 0) {
            pointerGrabSerial = 0;
            pointerGrabClient = nullptr;
            pointerGrabOrigin = nullptr;
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
        for (Popup* p : eachRev<Popup>(srv->scene->popups)) {
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
        wl_client* client = wl_resource_get_client(resOf(ptrFocus));
        bool delivered = false;

        for (wl_resource* p : pointers) {
            if (sameClientS(p, ptrFocus)) {
                wl_pointer_send_button(p, serial, t, button, pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
                delivered = true;

                if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                    wl_pointer_send_frame(p);
                }
            }
        }

        if (delivered) {
            rememberSerial(serial, client, ptrFocus);

            if (pressed && buttonsDown == 0) {
                pointerGrabSerial = serial;
                pointerGrabClient = client;
                pointerGrabOrigin = ptrFocus;
            }
        }
    }

    buttonsDown += pressed ? 1 : -1;

    if (buttonsDown < 0) {
        buttonsDown = 0;
    }

    if (buttonsDown == 0) {
        pointerGrabSerial = 0;
        pointerGrabClient = nullptr;
        pointerGrabOrigin = nullptr;
    }
}

void SeatState::handleScroll(const ScrollEvent& ev) {
    srv->scene->needsFrame = true;

    if (!ptrFocus) {
        return;
    }

    u32 t = nowMsec();

    for (wl_resource* p : pointers) {
        if (!sameClientS(p, ptrFocus)) {
            continue;
        }

        u32 pv = wl_resource_get_version(p);
        // v8 replaces axis_discrete with the high-resolution axis_value120;
        // send one or the other, never both
        bool value120 = pv >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION;
        bool discrete = !value120 && pv >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION;
        bool source = pv >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION;
        bool relDir = pv >= WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION;

        if (source) {
            u32 wlSource = ev.source == ScrollSource::wheel ? WL_POINTER_AXIS_SOURCE_WHEEL
                         : ev.source == ScrollSource::finger ? WL_POINTER_AXIS_SOURCE_FINGER
                                                            : WL_POINTER_AXIS_SOURCE_CONTINUOUS;

            wl_pointer_send_axis_source(p, wlSource);
        }

        // we never invert the sign relative to the physical motion; libinput's
        // natural-scroll config is already folded into the value
        auto sendRelDir = [&](u32 axis) {
            if (relDir) {
                wl_pointer_send_axis_relative_direction(p, axis, WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
            }
        };

        if (ev.dy != 0) {
            sendRelDir(WL_POINTER_AXIS_VERTICAL_SCROLL);

            if (ev.source == ScrollSource::wheel) {
                if (value120 && ev.value120Y != 0) {
                    wl_pointer_send_axis_value120(p, WL_POINTER_AXIS_VERTICAL_SCROLL, ev.value120Y);
                } else if (discrete && ev.discreteY != 0) {
                    wl_pointer_send_axis_discrete(p, WL_POINTER_AXIS_VERTICAL_SCROLL, ev.discreteY);
                }
            }

            wl_pointer_send_axis(p, t, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(ev.dy * 15.0));
        } else if (ev.stopY && pv >= WL_POINTER_AXIS_STOP_SINCE_VERSION) {
            wl_pointer_send_axis_stop(p, t, WL_POINTER_AXIS_VERTICAL_SCROLL);
        }

        if (ev.dx != 0) {
            sendRelDir(WL_POINTER_AXIS_HORIZONTAL_SCROLL);

            if (ev.source == ScrollSource::wheel) {
                if (value120 && ev.value120X != 0) {
                    wl_pointer_send_axis_value120(p, WL_POINTER_AXIS_HORIZONTAL_SCROLL, ev.value120X);
                } else if (discrete && ev.discreteX != 0) {
                    wl_pointer_send_axis_discrete(p, WL_POINTER_AXIS_HORIZONTAL_SCROLL, ev.discreteX);
                }
            }

            wl_pointer_send_axis(p, t, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(ev.dx * 15.0));
        } else if (ev.stopX && pv >= WL_POINTER_AXIS_STOP_SINCE_VERSION) {
            wl_pointer_send_axis_stop(p, t, WL_POINTER_AXIS_HORIZONTAL_SCROLL);
        }

        if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
            wl_pointer_send_frame(p);
        }
    }
}

Surface* SeatState::kbFocusSurface() {
    if (kbOverride) {
        return kbOverride;
    }

    return kbFocus ? kbFocus->surface : nullptr;
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

    // text-input focus follows keyboard focus
    wl_client* client = wl_resource_get_client(target);

    forEach<TextInput>(textInputs, [&](TextInput& ti) {
        if (ti.client == client) {
            // a leaving surface's text input goes inert: drop enabled so the
            // input method deactivates
            bool wasEnabled = ti.enabled;

            ti.enabled = false;
            zwp_text_input_v3_send_leave(ti.res, target);

            if (wasEnabled) {
                imUpdateActivation();
            }
        }
    });
}

void SeatState::kbSendEnter(wl_resource* target) {
    if (!target) {
        return;
    }

    wl_client* enterClient = wl_resource_get_client(target);

    forEach<TextInput>(textInputs, [&](TextInput& ti) {
        if (ti.client == enterClient) {
            zwp_text_input_v3_send_enter(ti.res, target);
        }
    });

    u32 serial = wl_display_next_serial(srv->display);
    wl_array keys;
    bool delivered = false;

    wl_array_init(&keys);

    for (u32 kc : pressedKeys) {
        *(u32*)wl_array_add(&keys, sizeof(u32)) = kc;
    }

    for (wl_resource* k : keyboards) {
        if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
            wl_keyboard_send_enter(k, serial, target, &keys);
            wl_keyboard_send_modifiers(k, wl_display_next_serial(srv->display), modsDepressed, modsLatched, modsLocked, modsGroup);
            delivered = true;
        }
    }

    if (delivered) {
        rememberSerial(serial, wl_resource_get_client(target), (Surface*)surfaceFrom(target));
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

    // an input method grab intercepts physical keys: they go to the IME's
    // keyboard-grab object, not the focused application
    if (imGrabActive()) {
        u32 t = nowMsec();
        u32 serial = wl_display_next_serial(srv->display);

        zwp_input_method_keyboard_grab_v2_send_key(inputMethod->grab, serial, t, code,
            pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);

        return;
    }

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
        wl_client* client = wl_resource_get_client(target);
        bool delivered = false;

        for (wl_resource* k : keyboards) {
            if (wl_resource_get_client(k) == client) {
                wl_keyboard_send_key(k, serial, t, code, pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
                delivered = true;
            }
        }

        if (delivered) {
            rememberSerial(serial, client, (Surface*)surfaceFrom(target));

            if (pressed) {
                keyGrabSerial = serial;
                keyGrabClient = client;
                keyGrabGeneration = focusGeneration;
            }
        }
    }
}

void SeatState::updateShortcutInhibit() {
    Surface* target = kbOverride ? kbOverride : kbFocus ? kbFocus->surface : nullptr;
    Surface* root = target ? target->rootSurface() : nullptr;
    bool inhibited = false;

    forEach<Surface, SceneNode>(srv->scene->surfaces, [&](Surface& surface) {
        auto& s = (SurfaceImpl&)surface;

        if (s.kbInhibitRes) {
            bool active = &surface == root;

            if (active != s.kbInhibitActive) {
                s.kbInhibitActive = active;

                if (active) {
                    zwp_keyboard_shortcuts_inhibitor_v1_send_active(s.kbInhibitRes);
                } else {
                    zwp_keyboard_shortcuts_inhibitor_v1_send_inactive(s.kbInhibitRes);
                }
            }

            inhibited = inhibited || active;
        }
    });

    srv->scene->shortcutsInhibited = inhibited;
}

void SeatState::rememberSerial(u32 serial, wl_client* client, Surface* surface) {
    constexpr size_t maxSerials = 64;

    if (inputSerials.length() == maxSerials) {
        for (size_t i = 1; i < inputSerials.length(); i++) {
            inputSerials.mut(i - 1) = inputSerials[i];
        }

        inputSerials.popBack();
    }

    inputSerials.pushBack({serial, client, surface, focusGeneration});
}

bool SeatState::validSerial(wl_client* client, u32 serial) const {
    for (size_t i = inputSerials.length(); i > 0; i--) {
        const InputSerial& entry = inputSerials[i - 1];

        if (entry.value == serial) {
            return entry.client == client &&
                   entry.focusGeneration == focusGeneration;
        }
    }

    return false;
}

void SeatState::rememberPointerEnter(wl_resource* pointer, u32 serial) {
    for (size_t i = 0; i < pointerEnters.length(); i++) {
        PointerEnter& entry = pointerEnters.mut(i);

        if (entry.pointer == pointer) {
            entry.serial = serial;

            return;
        }
    }

    pointerEnters.pushBack({pointer, serial});
}

bool SeatState::validPointerEnter(wl_resource* pointer, u32 serial) const {
    for (const PointerEnter& entry : pointerEnters) {
        if (entry.pointer == pointer) {
            return entry.serial == serial;
        }
    }

    return false;
}

void SeatState::forgetPointer(wl_resource* pointer) {
    for (size_t i = 0; i < pointerEnters.length(); i++) {
        if (pointerEnters[i].pointer == pointer) {
            pointerEnters.mut(i) = pointerEnters.back();
            pointerEnters.popBack();

            break;
        }
    }

    for (size_t i = 0; i < cursorShapeBindings.length(); i++) {
        if (cursorShapeBindings[i].pointer == pointer) {
            cursorShapeBindings.mut(i).pointer = nullptr;
        }
    }
}

bool SeatState::validCursorShapeEnter(wl_resource* device, u32 serial) const {
    for (const CursorShapeBinding& binding : cursorShapeBindings) {
        if (binding.device == device) {
            return binding.pointer && validPointerEnter(binding.pointer, serial);
        }
    }

    return false;
}

void SeatState::forgetCursorShapeDevice(wl_resource* device) {
    for (size_t i = 0; i < cursorShapeBindings.length(); i++) {
        if (cursorShapeBindings[i].device == device) {
            cursorShapeBindings.mut(i) = cursorShapeBindings.back();
            cursorShapeBindings.popBack();

            return;
        }
    }
}

bool SeatState::validSelectionSerial(wl_client* client, u32 serial) {
    wl_resource* target = kbTargetRes();

    return target && wl_resource_get_client(target) == client && validSerial(client, serial);
}

void SeatState::setSelection(wl_client* client, u32 serial, DataSource* src, bool primary) {
    if (!validSelectionSerial(client, serial)) {
        return;
    }

    applySelection(src, primary);
}

void SeatState::applySelection(DataSource* src, bool primary) {
    DataSource*& slot = primary ? primarySel : clipboard;

    if (slot == src) {
        return;
    }

    if (slot) {
        if (slot->dc) {
            ext_data_control_source_v1_send_cancelled(slot->res);
        } else if (primary) {
            zwp_primary_selection_source_v1_send_cancelled(slot->res);
        } else {
            wl_data_source_send_cancelled(slot->res);
        }
    }

    slot = src;

    if (wl_resource* target = kbTargetRes()) {
        sendSelections(wl_resource_get_client(target));
    }

    sendDcSelections();
}

// data-control devices see every selection change, focus-free
void SeatState::sendDcSelections() {
    for (wl_resource* d : dcDevices) {
        ext_data_control_device_v1_send_selection(
            d, clipboard ? makeDcOffer(d, clipboard) : nullptr);
        ext_data_control_device_v1_send_primary_selection(
            d, primarySel ? makeDcOffer(d, primarySel) : nullptr);
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
        // tell the target the dnd session is over, or it keeps its drop zone
        // highlighted waiting for a drop that never comes
        if (dragTarget) {
            for (wl_resource* d : dataDevices) {
                if (sameClientS(d, dragTarget)) {
                    wl_data_device_send_leave(d);
                }
            }
        }

        dragSource = nullptr;
        dragClient = nullptr;
        dragTarget = nullptr;
        srv->scene->dragIcon = nullptr;
        srv->scene->needsFrame = true;
    }

    if (resend) {
        if (wl_resource* target = kbTargetRes()) {
            sendSelections(wl_resource_get_client(target));
        }

        sendDcSelections();
    }
}

void SeatState::startDrag(wl_client* client, u32 serial, DataSource* src, Surface* origin, Surface* icon) {
    if (buttonsDown <= 0 || serial != pointerGrabSerial || client != pointerGrabClient || origin != pointerGrabOrigin) {
        // cancelled exists since v1; see deviceStartDrag
        if (src) {
            wl_data_source_send_cancelled(src->res);
        }

        return;
    }

    dragSource = src;
    dragClient = client;
    dragTarget = nullptr;
    srv->scene->dragIcon = icon;
    srv->scene->needsFrame = true;
    pointerSetFocus(nullptr, 0, 0);
    dragMotion();
}

void SeatState::dragMotion() {
    Surface* target = pickPointerTarget();

    if (!dragSource && target && wl_resource_get_client(resOf(target)) != dragClient) {
        target = nullptr;
    }

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

                wl_resource* offer = dragSource ? makeOffer(d, dragSource, true) : nullptr;

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
    dragClient = nullptr;
    srv->scene->dragIcon = nullptr;
    srv->scene->needsFrame = true;

    // a drop only lands on a target that accepted a mime type; otherwise
    // the spec calls for cancelling the operation (the target still gets
    // its leave). without a source (client-internal drag) accept state is
    // unknowable — deliver the drop as before
    bool accepted = !src;

    if (src) {
        forEach<Offer>(src->offers, [&](Offer& offer) {
            if (offer.dnd && offer.accepted) {
                accepted = true;
            }
        });
    }

    if (dragTarget && accepted) {
        for (wl_resource* d : dataDevices) {
            if (sameClientS(d, dragTarget)) {
                wl_data_device_send_drop(d);
            }
        }

        if (src) {
            src->dropPerformed = true;
        }

        if (src && wl_resource_get_version(src->res) >= 3) {
            wl_data_source_send_dnd_drop_performed(src->res);
        }
    } else {
        if (dragTarget) {
            for (wl_resource* d : dataDevices) {
                if (sameClientS(d, dragTarget)) {
                    wl_data_device_send_leave(d);
                }
            }
        }

        if (src) {
            wl_data_source_send_cancelled(src->res);
        }
    }

    dragTarget = nullptr;
}

TextInput* SeatState::activeTextInput() {
    wl_resource* target = kbTargetRes();

    if (!target) {
        return nullptr;
    }

    wl_client* client = wl_resource_get_client(target);
    TextInput* found = nullptr;

    forEach<TextInput>(textInputs, [&](TextInput& ti) {
        if (!found && ti.enabled && ti.client == client) {
            found = &ti;
        }
    });

    return found;
}

bool SeatState::imGrabActive() const {
    return inputMethod && inputMethod->grab;
}

// drive the input method's activation from the current focus + enable state
void SeatState::imUpdateActivation() {
    InputMethod* im = inputMethod;

    if (!im) {
        return;
    }

    TextInput* ti = activeTextInput();

    if (ti && !im->active) {
        im->active = true;
        zwp_input_method_v2_send_activate(im->res);

        if (ti->surroundingSet) {
            Buffer b(sv(ti->surrounding));
            zwp_input_method_v2_send_surrounding_text(im->res, b.cStr(), (u32)ti->cursor, (u32)ti->anchor);
        }

        if (ti->contentSet) {
            zwp_input_method_v2_send_content_type(im->res, ti->hint, ti->purpose);
        }

        zwp_input_method_v2_send_done(im->res);
    } else if (ti && im->active) {
        bool any = false;

        if (ti->surroundingSet) {
            Buffer b(sv(ti->surrounding));
            zwp_input_method_v2_send_surrounding_text(im->res, b.cStr(), (u32)ti->cursor, (u32)ti->anchor);
            any = true;
        }

        if (ti->contentSet) {
            zwp_input_method_v2_send_content_type(im->res, ti->hint, ti->purpose);
            any = true;
        }

        if (any) {
            zwp_input_method_v2_send_done(im->res);
        }
    } else if (!ti && im->active) {
        im->active = false;
        zwp_input_method_v2_send_deactivate(im->res);
        zwp_input_method_v2_send_done(im->res);
    }

    imUpdatePopup();
}

// the input method committed: push its staged string/preedit/delete into the
// active text input and bump its serial
void SeatState::imFlushToTextInput() {
    InputMethod* im = inputMethod;
    TextInput* ti = activeTextInput();

    if (!im || !ti) {
        if (im) {
            im->commitSet = im->preeditSet = false;
            im->deleteBefore = im->deleteAfter = 0;
        }

        return;
    }

    if (im->deleteBefore || im->deleteAfter) {
        zwp_text_input_v3_send_delete_surrounding_text(ti->res, im->deleteBefore, im->deleteAfter);
    }

    if (im->preeditSet) {
        Buffer b(sv(im->preeditStr));
        zwp_text_input_v3_send_preedit_string(ti->res, im->preeditStr.empty() ? nullptr : b.cStr(), im->preeditBegin, im->preeditEnd);
    } else {
        zwp_text_input_v3_send_preedit_string(ti->res, nullptr, 0, 0);
    }

    if (im->commitSet) {
        Buffer b(sv(im->commitStr));
        zwp_text_input_v3_send_commit_string(ti->res, im->commitStr.empty() ? nullptr : b.cStr());
    }

    zwp_text_input_v3_send_done(ti->res, ++ti->serial);

    im->commitSet = im->preeditSet = false;
    im->commitStr.reset();
    im->preeditStr.reset();
    im->preeditBegin = im->preeditEnd = 0;
    im->deleteBefore = im->deleteAfter = 0;
}

// wp input popup: send the text-input rectangle to the popup surface and
// place it in the scene at the active text input's cursor, screen-relative
void SeatState::imUpdatePopup() {
    InputMethod* im = inputMethod;

    if (!im || !im->popupRes) {
        return;
    }

    TextInput* ti = activeTextInput();

    if (!ti || !im->active) {
        if (srv->scene->imePopup == (Surface*)im->popupSurface) {
            srv->scene->imePopup = nullptr;
        }

        return;
    }

    // the rectangle event is in the popup surface's coordinates: the anchor
    // is the text-input cursor rect's bottom-left, so the rect origin is
    // negative by the cursor rect offset
    RectI r = ti->rectSet ? ti->rect : RectI{0, 0, 0, 0};
    RectI popupRect{-r.x, -r.y, r.w, r.h};

    if (popupRect.x != im->lastRect.x || popupRect.y != im->lastRect.y ||
        popupRect.w != im->lastRect.w || popupRect.h != im->lastRect.h) {
        im->lastRect = popupRect;
        zwp_input_popup_surface_v2_send_text_input_rectangle(im->popupRes, popupRect.x, popupRect.y, r.w, r.h);
    }

    // place the popup below the cursor rectangle, in screen coordinates
    if (im->popupSurface && im->popupSurface->texture) {
        Surface* anchor = kbFocusSurface();

        if (anchor) {
            srv->scene->imePopup = (Surface*)im->popupSurface;
            srv->scene->imePopupX = anchor->imgX + (float)anchor->geomX() + (float)r.x;
            srv->scene->imePopupY = anchor->imgY + (float)anchor->geomY() + (float)r.y + (float)r.h;
        }
    }
}

void SeatState::focusToplevel(Toplevel* t) {
    if (kbFocus == t) {
        return;
    }

    focusGeneration++;

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

    // under an active popup grab the keyboard belongs to kbOverride: the old
    // focus already got its leave at grab start, and the new one gets enter
    // when the grab ends (popupGone) — sending either here would show the
    // client two focused surfaces at once
    if (kbFocus && kbFocus->surface && !kbOverride) {
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

    if (t && t->surface && !kbOverride) {
        kbSendEnter(resOf(t->surface));
        sysO << "imway: focus -> "_sv << sv(t->title) << endL;
    }

    updateShortcutInhibit();
}

// unmap (null-buffer commit) hides the window but keeps it alive: without a
// focus handoff the keyboard and pointer keep feeding an invisible surface
void SeatState::toplevelUnmapped(Toplevel* t) {
    if (ptrFocus && ptrFocus->rootToplevel() == t) {
        pointerSetFocus(nullptr, 0, 0);
        buttonsDown = 0;
    }

    if (kbFocus == t) {
        Toplevel* next = nullptr;

        for (Toplevel* other : eachRev<Toplevel>(srv->scene->toplevels)) {
            if (other != t && other->mapped) {
                next = other;

                break;
            }
        }

        // unlike toplevelGone the surface is still alive, so focusToplevel
        // delivers the leave to it before entering the next window
        focusToplevel(next);
    }
}

void SeatState::popupGrabStart(Popup* p) {
    if (!p->surface) {
        return;
    }

    kbSendLeave(kbTargetRes());
    focusGeneration++;
    grabStack.pushBack((GrabNode*)p->surface);
    kbOverride = p->surface;
    kbSendEnter(resOf(kbOverride));
    updateShortcutInhibit();
}

void SeatState::grabGone(Surface* s, bool sendLeave) {
    // a surface outside the stack has a self-linked grab node
    if (((GrabNode*)s)->singular()) {
        return;
    }

    bool wasTop = kbOverride == s;

    ((GrabNode*)s)->unlink();

    if (!wasTop) {
        return;
    }

    if (sendLeave) {
        kbSendLeave(resOf(s));
    }

    focusGeneration++;
    kbOverride = grabStack.empty() ? nullptr : (Surface*)(GrabNode*)grabStack.mutBack();
    kbSendEnter(kbTargetRes());
    updateShortcutInhibit();
}

void SeatState::popupGone(Popup* p) {
    Surface* s = p->surface;

    if (s && ptrFocus && ptrFocus->rootSurface() == s) {
        pointerSetFocus(nullptr, 0, 0);
        buttonsDown = 0;
        pointerGrabSerial = 0;
        pointerGrabClient = nullptr;
        pointerGrabOrigin = nullptr;
    }

    if (s) {
        grabGone(s, true);
    }
}

void SeatState::surfaceGone(Surface* s) {
    if (ptrFocus == s) {
        ptrFocus = nullptr;
        buttonsDown = 0;
    }

    // the wl_surface can die before its xdg_popup role: popupGone then sees
    // p->surface == null and would leave the grab stack dangling forever
    grabGone(s, false);

    size_t keep = 0;

    for (size_t i = 0; i < inputSerials.length(); i++) {
        if (inputSerials[i].surface != s) {
            inputSerials.mut(keep++) = inputSerials[i];
        }
    }

    while (inputSerials.length() > keep) {
        inputSerials.popBack();
    }

    if (pointerGrabOrigin == s) {
        pointerGrabSerial = 0;
        pointerGrabClient = nullptr;
        pointerGrabOrigin = nullptr;
    }
}

void SeatState::toplevelGone(Toplevel* t) {
    if (ptrFocus && ptrFocus->rootToplevel() == t) {
        ptrFocus = nullptr;
        buttonsDown = 0;
    }

    if (kbFocus == t) {
        kbFocus = nullptr;
        focusGeneration++;

        for (Toplevel* other : eachRev<Toplevel>(srv->scene->toplevels)) {
            if (other != t && other->mapped) {
                focusToplevel(other);

                break;
            }
        }
    }

    // if the dying window held a shortcuts inhibitor and was the last one,
    // no focus change recomputes the flag and hotkeys stay dead forever
    updateShortcutInhibit();
}

WaylandImpl::WaylandImpl(Composer& comp, const WaylandConfig& cfg)
    : composer(&comp)
    , pool(comp.pool)
    , alloc(comp.alloc)
    , loop(comp.loop)
    , scene(comp.scene)
    , socketName(cfg.socketName)
    , keyboard(comp.kb)
    , mainDevice(cfg.mainDevice)
    , seat(*this)
{
    formats.append(cfg.formats, cfg.formatCount);

    // scanout tranche indices into the shared format table: only pairs the
    // render device can also sample qualify
    for (size_t i = 0; i < cfg.scanoutFormatCount; i++) {
        for (u16 j = 0; j < (u16)formats.length(); j++) {
            if (formats[j].fourcc == cfg.scanoutFormats[i].fourcc &&
                formats[j].modifier == cfg.scanoutFormats[i].modifier) {
                scanoutIndices.pushBack(j);
                break;
            }
        }
    }

    display = wl_display_create();
    STD_VERIFY(display);

    wlLoop = wl_display_get_event_loop(display);

    output = cfg.output;
    cmDisplayColor = output ? output->colorState() : OutputColorState::sdr();
    cmDisplayIdentity = ++cimgIdentity;
    dpmsSec = cfg.dpmsSec;
    iconPool = comp.iconPool;
    icons = comp.icons;
    comp.iconListeners.pushBack(comp.pool->make<CallIconsReloaded>(this));
    comp.sessionEnabledListeners.pushBack(comp.pool->make<CallWaylandSessionEnabled>(this));
    comp.sessionDisabledListeners.pushBack(comp.pool->make<CallWaylandSessionDisabled>(this));
    comp.frameListeners.pushBack((Listener*)this);
    comp.inputSinks.pushBack((InputSink*)this);
    drmFd = cfg.drmFd;
    explicitSyncSupported = cfg.explicitSync;

    if (output && dpmsSec > 0) {
        ev_timer_init(&dpmsTimer, dpmsTimerCb, dpmsSec, dpmsSec);
        dpmsTimer.data = this;
        ev_timer_again(loop, &dpmsTimer);
    }

    if (wl_display_add_socket(display, Buffer(socketName).cStr()) != 0) {
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

    // ---- color-management-v1 ----

    void cmImageDescDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    u32 cmTfNamed(const ColorDescription& d, u32 version) {
        if (d.transfer == ColorTransfer::pq) {
            return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ;
        }

        if (d.transfer == ColorTransfer::hlg) {
            return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG;
        }

        if (d.transfer == ColorTransfer::extendedLinear) {
            return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR;
        }

        if (d.transfer == ColorTransfer::bt1886) {
            return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886;
        }

        if (d.transfer == ColorTransfer::gamma22) {
            return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22;
        }

        return version >= 2 ? WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_COMPOUND_POWER_2_4 :
                              WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB;
    }

    u32 cmPrimariesNamed(const ColorDescription& d) {
        return d.primaries == ColorPrimaries::bt2020 ?
            WP_COLOR_MANAGER_V1_PRIMARIES_BT2020 :
            d.primaries == ColorPrimaries::displayP3 ?
            WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3 :
            WP_COLOR_MANAGER_V1_PRIMARIES_SRGB;
    }

    u32 cmMinLuminance(double nits) {
        return (u32)(nits * 10000.0 + .5);
    }

    u32 cmLuminance(double nits) {
        return (u32)(nits + .5);
    }

    void cmImageDescGetInfo(wl_client* client, wl_resource* res, u32 id) {
        auto* d = (CImgDesc*)wl_resource_get_user_data(res);

        if (!d || !d->ready) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY,
                                   "this image description is not ready");

            return;
        }

        if (!d->allowInfo) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_V1_ERROR_NO_INFORMATION,
                                   "this image description cannot be introspected");

            return;
        }

        wl_resource* info = wl_resource_create(client, &wp_image_description_info_v1_interface,
                                               wl_resource_get_version(res), id);

        if (!info) {
            wl_client_post_no_memory(client);

            return;
        }

        const ColorDescription& color = d->color;
        const Chromaticities& primary = color.primary;
        const Chromaticities& target = color.target;

        if (color.primaries != ColorPrimaries::custom) {
            wp_image_description_info_v1_send_primaries_named(info, cmPrimariesNamed(color));
        }
        wp_image_description_info_v1_send_primaries(
            info, primary.rx, primary.ry, primary.gx, primary.gy,
            primary.bx, primary.by, primary.wx, primary.wy);
        wp_image_description_info_v1_send_tf_named(
            info, cmTfNamed(color, wl_resource_get_version(res)));
        wp_image_description_info_v1_send_luminances(
            info, cmMinLuminance(color.minNits), cmLuminance(color.maxNits),
            cmLuminance(color.referenceNits));
        wp_image_description_info_v1_send_target_primaries(
            info, target.rx, target.ry, target.gx, target.gy,
            target.bx, target.by, target.wx, target.wy);
        wp_image_description_info_v1_send_target_luminance(
            info, cmMinLuminance(color.targetMinNits), cmLuminance(color.targetMaxNits));

        if (color.maxCllSet) {
            wp_image_description_info_v1_send_target_max_cll(info, color.maxCll);
        }

        if (color.maxFallSet) {
            wp_image_description_info_v1_send_target_max_fall(info, color.maxFall);
        }

        wp_image_description_info_v1_send_done(info);
        wl_resource_destroy(info);
    }

    const struct wp_image_description_v1_interface cmImageDescImpl = {
        .destroy = cmImageDescDestroy,
        .get_information = cmImageDescGetInfo,
    };

    void cmImageDescResourceDestroyed(wl_resource* res) {
        auto* d = (CImgDesc*)wl_resource_get_user_data(res);

        if (d && d->srv) {
            d->srv->alloc->release(d);
        }
    }

    // build an image description resource carrying `d`, sent ready
    wl_resource* cmMakeImageDesc(WaylandImpl* srv, wl_client* client, u32 version, u32 id, const CImgDesc& d) {
        wl_resource* res = wl_resource_create(client, &wp_image_description_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return nullptr;
        }

        CImgDesc* obj = srv->alloc->make<CImgDesc>();

        *obj = d;
        obj->srv = srv;

        if (!obj->identity) {
            obj->identity = ++srv->cimgIdentity;
        }

        wl_resource_set_implementation(res, &cmImageDescImpl, obj, cmImageDescResourceDestroyed);

        if (version >= 2) {
            wp_image_description_v1_send_ready2(res, (u32)(obj->identity >> 32), (u32)obj->identity);
        } else {
            wp_image_description_v1_send_ready(res, (u32)obj->identity);
        }

        return res;
    }

    wl_resource* cmMakeFailedImageDesc(WaylandImpl* srv, wl_client* client,
                                        u32 version, u32 id, u32 cause,
                                        const char* message) {
        wl_resource* res = wl_resource_create(client, &wp_image_description_v1_interface,
                                              version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return nullptr;
        }

        CImgDesc* obj = srv->alloc->make<CImgDesc>();

        *obj = {};
        obj->srv = srv;
        obj->ready = false;
        wl_resource_set_implementation(res, &cmImageDescImpl, obj,
                                       cmImageDescResourceDestroyed);
        wp_image_description_v1_send_failed(res, cause, message);

        return res;
    }

    // params creator
    void cmParamsSetTfNamed(wl_client*, wl_resource* res, u32 tf) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        if (p->tfSet) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                                   "transfer function is already set");

            return;
        }

        u32 version = wl_resource_get_version(res);
        bool sRgb = version >= 2 ?
            tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_COMPOUND_POWER_2_4 :
            tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB;

        bool pq = tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ;
        bool hlg = tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG;
        bool linear = tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR;
        bool bt1886 = tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886;
        bool gamma22 = tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22;

        if (!sRgb && !pq && !hlg && !linear && !bt1886 && !gamma22) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_TF,
                                   "transfer function was not advertised");

            return;
        }

        p->d.color.transfer = pq ? ColorTransfer::pq :
                              hlg ? ColorTransfer::hlg :
                              linear ? ColorTransfer::extendedLinear :
                              bt1886 ? ColorTransfer::bt1886 :
                              gamma22 ? ColorTransfer::gamma22 : ColorTransfer::sRgb;

        if (linear) {
            p->d.color.linearOneNits = p->d.color.referenceNits;
        }

        if (bt1886 && !p->lumSet) {
            ColorDescription defaults = ColorDescription::bt1886();

            p->d.color.minNits = defaults.minNits;
            p->d.color.maxNits = defaults.maxNits;
            p->d.color.referenceNits = defaults.referenceNits;
            p->d.color.targetMinNits = defaults.targetMinNits;
            p->d.color.targetMaxNits = defaults.targetMaxNits;
        }

        if ((pq || hlg) && !p->lumSet) {
            ColorDescription defaults = pq ? ColorDescription::bt2100Pq() :
                                                ColorDescription::bt2100Hlg();

            p->d.color.minNits = defaults.minNits;
            p->d.color.maxNits = defaults.maxNits;
            p->d.color.referenceNits = defaults.referenceNits;
            p->d.color.targetMinNits = defaults.targetMinNits;
            p->d.color.targetMaxNits = defaults.targetMaxNits;
        } else if (pq) {
            p->d.color.maxNits = 10000.0 + p->d.color.minNits;
            p->d.color.targetMaxNits = p->d.color.maxNits;
        }

        p->tfSet = true;
    }

    void cmParamsSetTfPower(wl_client*, wl_resource* res, u32 eexp) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        if (p->tfSet) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                                   "the transfer function is already set");

            return;
        }

        // wire units are 1/10000 of the exponent; 1.0..10.0 is the valid range
        double g = (double)eexp / 10000.0;

        if (g < 1.0 || g > 10.0) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_TF,
                                   "power exponent out of range");

            return;
        }

        p->tfSet = true;
        p->d.color.transfer = ColorTransfer::iccGamma;
        p->d.color.gamma[0] = p->d.color.gamma[1] = p->d.color.gamma[2] = g;
    }

    // wp_color_manager_v1 named primaries -> chromaticities in 1/1000000
    // units (D65 white unless noted). false = not a primaries name we map
    bool namedPrimaries(u32 prim, Chromaticities& out) {
        switch (prim) {
            case WP_COLOR_MANAGER_V1_PRIMARIES_SRGB:
                out = Chromaticities::sRgb();
                return true;
            case WP_COLOR_MANAGER_V1_PRIMARIES_BT2020:
                out = Chromaticities::bt2020();
                return true;
            case WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3:
                out = Chromaticities::displayP3();
                return true;
            case WP_COLOR_MANAGER_V1_PRIMARIES_PAL_M:
                out = {670000, 330000, 210000, 710000, 140000, 80000, 310000, 316000};
                return true;
            case WP_COLOR_MANAGER_V1_PRIMARIES_PAL:
                out = {640000, 330000, 290000, 600000, 150000, 60000, 313000, 329000};
                return true;
            case WP_COLOR_MANAGER_V1_PRIMARIES_NTSC:
                out = {630000, 340000, 310000, 595000, 155000, 70000, 312727, 329024};
                return true;
            case WP_COLOR_MANAGER_V1_PRIMARIES_GENERIC_FILM:
                out = {681000, 319000, 243000, 692000, 145000, 49000, 310000, 316000};
                return true;
            case WP_COLOR_MANAGER_V1_PRIMARIES_DCI_P3:
                // DCI white, not D65
                out = {680000, 320000, 265000, 690000, 150000, 60000, 314000, 351000};
                return true;
            case WP_COLOR_MANAGER_V1_PRIMARIES_ADOBE_RGB:
                out = {640000, 330000, 210000, 710000, 150000, 60000, 313000, 329000};
                return true;
            default:
                return false;
        }
    }

    void cmParamsSetPrimNamed(wl_client*, wl_resource* res, u32 prim) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        if (p->primSet) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                                   "primaries are already set");

            return;
        }

        Chromaticities chroma;

        if (!namedPrimaries(prim, chroma)) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_PRIMARIES_NAMED,
                                   "primaries were not advertised");

            return;
        }

        p->d.color.primaries = prim == WP_COLOR_MANAGER_V1_PRIMARIES_BT2020 ?
            ColorPrimaries::bt2020 :
            prim == WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3 ?
            ColorPrimaries::displayP3 :
            prim == WP_COLOR_MANAGER_V1_PRIMARIES_SRGB ?
            ColorPrimaries::sRgb : ColorPrimaries::custom;
        p->d.color.primary = chroma;
        p->d.color.target = chroma;
        p->primSet = true;
    }

    void cmParamsSetPrim(wl_client*, wl_resource* res,
                         i32 rx, i32 ry, i32 gx, i32 gy,
                         i32 bx, i32 by, i32 wx, i32 wy) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        if (p->primSet) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                                   "primaries are already set");

            return;
        }

        p->d.color.primaries = ColorPrimaries::custom;
        p->d.color.primary = {rx, ry, gx, gy, bx, by, wx, wy};
        p->d.color.target = p->d.color.primary;
        p->primSet = true;
    }

    bool cmInvalidLumRange(u32 minLum, u32 maxLum) {
        return (u64)maxLum * 10000 <= minLum;
    }

    void cmParamsSetLum(wl_client*, wl_resource* res, u32 minLum, u32 maxLum, u32 refLum) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        if (p->lumSet) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET,
                                   "primary luminances are already set");

            return;
        }

        if (cmInvalidLumRange(minLum, refLum) ||
            (p->tfSet && !p->d.color.hdr() && cmInvalidLumRange(minLum, maxLum))) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
                                   "primary max and reference luminance must exceed min luminance");

            return;
        }

        p->lumSet = true;
        p->d.color.minNits = (double)minLum / 10000.0;
        p->d.color.maxNits = p->d.color.transfer == ColorTransfer::pq ?
            10000.0 + p->d.color.minNits : (double)maxLum;
        p->d.color.referenceNits = (double)refLum;
        if (p->d.color.transfer == ColorTransfer::extendedLinear) {
            p->d.color.linearOneNits = p->d.color.referenceNits;
        }
        p->d.color.targetMinNits = p->d.color.minNits;
        p->d.color.targetMaxNits = p->d.color.maxNits;
    }

    void cmParamsSetMasteringPrim(wl_client*, wl_resource* res,
                                  i32 rx, i32 ry, i32 gx, i32 gy,
                                  i32 bx, i32 by, i32 wx, i32 wy) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        // the target volume the content was mastered against: our tone map
        // reads it as the target chromaticities
        p->d.color.target = {rx, ry, gx, gy, bx, by, wx, wy};
    }

    void cmParamsSetMasteringLum(wl_client*, wl_resource* res, u32 minLum, u32 maxLum) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        // min in 1/10000 cd/m^2, max in cd/m^2
        p->d.color.targetMinNits = (double)minLum / 10000.0;
        p->d.color.targetMaxNits = (double)maxLum;
    }

    void cmParamsSetMaxCll(wl_client*, wl_resource* res, u32 v) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        p->d.color.maxCll = v;
        p->d.color.maxCllSet = true;
        p->maxCllSet = true;
    }

    void cmParamsSetMaxFall(wl_client*, wl_resource* res, u32 v) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        p->d.color.maxFall = v;
        p->d.color.maxFallSet = true;
        p->maxFallSet = true;
    }

    void cmParamsCreate(wl_client* client, wl_resource* res, u32 id) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        if (!p->tfSet || !p->primSet) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INCOMPLETE_SET, "transfer function and primaries are required");

            return;
        }

        u32 minLum = cmMinLuminance(p->d.color.minNits);
        u32 maxLum = cmLuminance(p->d.color.maxNits);

        if (p->d.color.transfer != ColorTransfer::pq && cmInvalidLumRange(minLum, maxLum)) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
                                   "primary max luminance must exceed min luminance");

            return;
        }

        auto levelInMasteringRange = [minLum, maxLum](u32 level) {
            return (u64)level * 10000 > minLum && level <= maxLum;
        };

        bool version1RangeError = wl_resource_get_version(res) < 2 &&
            ((p->maxCllSet && !levelInMasteringRange(p->d.color.maxCll)) ||
             (p->maxFallSet && !levelInMasteringRange(p->d.color.maxFall)));

        if (version1RangeError ||
            (p->maxCllSet && p->maxFallSet && p->d.color.maxFall > p->d.color.maxCll)) {
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE,
                                   "content light levels are outside the mastering luminance range");

            return;
        }

        if (!p->d.color.primary.valid()) {
            cmMakeFailedImageDesc(p->srv, client, wl_resource_get_version(res), id,
                                  WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
                                  "degenerate primaries");
            wl_resource_destroy(res);

            return;
        }

        cmMakeImageDesc(p->srv, client, wl_resource_get_version(res), id, p->d);
        wl_resource_destroy(res); // create consumes the creator
    }

    const struct wp_image_description_creator_params_v1_interface cmParamsImpl = {
        .create = cmParamsCreate,
        .set_tf_named = cmParamsSetTfNamed,
        .set_tf_power = cmParamsSetTfPower,
        .set_primaries_named = cmParamsSetPrimNamed,
        .set_primaries = cmParamsSetPrim,
        .set_luminances = cmParamsSetLum,
        .set_mastering_display_primaries = cmParamsSetMasteringPrim,
        .set_mastering_luminance = cmParamsSetMasteringLum,
        .set_max_cll = cmParamsSetMaxCll,
        .set_max_fall = cmParamsSetMaxFall,
    };

    void cmParamsResourceDestroyed(wl_resource* res) {
        auto* p = (CParams*)wl_resource_get_user_data(res);

        if (p && p->srv) {
            p->srv->alloc->release(p);
        }
    }

    // surface color object: user_data = the SurfaceImpl
    void cmSurfaceDestroy(wl_client*, wl_resource* res) {
        if (auto* s = (SurfaceImpl*)wl_resource_get_user_data(res)) {
            s->pendColorChanged = true;

            if (!s->pendColor) {
                s->pendColor = s->srv->alloc->make<ColorDescription>();
            }

            *s->pendColor = ColorDescription::sRgb();
        }

        wl_resource_destroy(res);
    }

    void cmSurfaceSetImageDesc(wl_client*, wl_resource* res, wl_resource* descRes, u32 intent) {
        auto* s = (SurfaceImpl*)wl_resource_get_user_data(res);
        auto* d = descRes ? (CImgDesc*)wl_resource_get_user_data(descRes) : nullptr;

        if (!s) {
            wl_resource_post_error(res, WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_INERT,
                                   "the wl_surface is gone");

            return;
        }

        if (!d || !d->ready) {
            wl_resource_post_error(res, WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_IMAGE_DESCRIPTION,
                                   "image description is not ready");

            return;
        }

        if (intent != WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL) {
            wl_resource_post_error(res, WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_RENDER_INTENT,
                                   "render intent was not advertised");

            return;
        }

        s->pendColorChanged = true;

        if (!s->pendColor) {
            s->pendColor = s->srv->alloc->make<ColorDescription>();
        }

        *s->pendColor = d->color;
    }

    void cmSurfaceUnsetImageDesc(wl_client*, wl_resource* res) {
        auto* s = (SurfaceImpl*)wl_resource_get_user_data(res);

        if (!s) {
            wl_resource_post_error(res, WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_INERT,
                                   "the wl_surface is gone");

            return;
        }

        s->pendColorChanged = true;

        if (!s->pendColor) {
            s->pendColor = s->srv->alloc->make<ColorDescription>();
        }

        *s->pendColor = ColorDescription::sRgb();
    }

    const struct wp_color_management_surface_v1_interface cmSurfaceImpl = {
        .destroy = cmSurfaceDestroy,
        .set_image_description = cmSurfaceSetImageDesc,
        .unset_image_description = cmSurfaceUnsetImageDesc,
    };

    void cmSurfaceResourceDestroyed(wl_resource* res) {
        if (auto* s = (SurfaceImpl*)wl_resource_get_user_data(res)) {
            s->colorRes = nullptr;
        }
    }

    // Output and preferred descriptions refer to the same immutable record and
    // therefore carry the same identity until the output color state changes.
    CImgDesc cmDisplayDesc(WaylandImpl* srv, u32 version) {
        (void)version;
        CImgDesc d;

        d.identity = srv->cmDisplayIdentity;
        d.allowInfo = true;
        d.color = srv->cmDisplayColor.encoding;

        return d;
    }

    void cmOutputDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void cmOutputGetImageDesc(wl_client* client, wl_resource* res, u32 id) {
        auto* obj = (CmOutput*)wl_resource_get_user_data(res);

        if (!obj->srv->output) {
            cmMakeFailedImageDesc(obj->srv, client, wl_resource_get_version(res), id,
                                  WP_IMAGE_DESCRIPTION_V1_CAUSE_NO_OUTPUT,
                                  "the wl_output is gone");

            return;
        }

        cmMakeImageDesc(obj->srv, client, wl_resource_get_version(res), id,
                        cmDisplayDesc(obj->srv, wl_resource_get_version(res)));
    }

    const struct wp_color_management_output_v1_interface cmOutputImpl = {
        .destroy = cmOutputDestroy,
        .get_image_description = cmOutputGetImageDesc,
    };

    void cmOutputResourceDestroyed(wl_resource* res) {
        auto* obj = (CmOutput*)wl_resource_get_user_data(res);

        if (obj) {
            removeOne(obj->srv->cmOutputResources, obj);
            obj->srv->alloc->release(obj);
        }
    }

    void cmFeedbackDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void cmFeedbackGetPreferred(wl_client* client, wl_resource* res, u32 id) {
        auto* obj = (CmFeedback*)wl_resource_get_user_data(res);

        if (!obj->surface) {
            wl_resource_post_error(res, WP_COLOR_MANAGEMENT_SURFACE_FEEDBACK_V1_ERROR_INERT,
                                   "the wl_surface is gone");

            return;
        }

        cmMakeImageDesc(obj->srv, client, wl_resource_get_version(res), id,
                        cmDisplayDesc(obj->srv, wl_resource_get_version(res)));
    }

    const struct wp_color_management_surface_feedback_v1_interface cmFeedbackImpl = {
        .destroy = cmFeedbackDestroy,
        .get_preferred = cmFeedbackGetPreferred,
        .get_preferred_parametric = cmFeedbackGetPreferred,
    };

    void cmFeedbackSurfaceDestroyed(wl_listener* listener, void*) {
        auto* obj = (CmFeedback*)((char*)listener - offsetof(CmFeedback, surfaceDestroy));

        obj->surface = nullptr;
        wl_list_remove(&listener->link);
    }

    void cmFeedbackResourceDestroyed(wl_resource* res) {
        auto* obj = (CmFeedback*)wl_resource_get_user_data(res);

        if (obj) {
            if (obj->surface) {
                wl_list_remove(&obj->surfaceDestroy.link);
            }

            removeOne(obj->srv->cmFeedbackResources, obj);
            obj->srv->alloc->release(obj);
        }
    }

    // manager
    void cmManagerDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void cmManagerGetOutput(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        wl_resource* out = wl_resource_create(client, &wp_color_management_output_v1_interface, wl_resource_get_version(res), id);

        if (!out) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        CmOutput* obj = srv->alloc->make<CmOutput>();

        *obj = {};
        obj->srv = srv;
        obj->res = out;
        wl_resource_set_implementation(out, &cmOutputImpl, obj, cmOutputResourceDestroyed);
        srv->cmOutputResources.pushBack(obj);
    }

    void cmManagerGetSurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        auto* surface = (SurfaceImpl*)wl_resource_get_user_data(surfaceRes);

        if (surface->colorRes) {
            wl_resource_post_error(res, WP_COLOR_MANAGER_V1_ERROR_SURFACE_EXISTS,
                                   "surface already has a color-management object");

            return;
        }

        wl_resource* cs = wl_resource_create(client, &wp_color_management_surface_v1_interface, wl_resource_get_version(res), id);

        if (!cs) {
            wl_client_post_no_memory(client);

            return;
        }

        surface->colorRes = cs;
        wl_resource_set_implementation(cs, &cmSurfaceImpl, surface, cmSurfaceResourceDestroyed);
    }

    void cmManagerGetSurfaceFeedback(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        wl_resource* fb = wl_resource_create(client, &wp_color_management_surface_feedback_v1_interface, wl_resource_get_version(res), id);

        if (!fb) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        CmFeedback* obj = srv->alloc->make<CmFeedback>();

        *obj = {};
        obj->srv = srv;
        obj->res = fb;
        obj->surface = (SurfaceImpl*)wl_resource_get_user_data(surfaceRes);
        obj->surfaceDestroy.notify = cmFeedbackSurfaceDestroyed;
        wl_resource_add_destroy_listener(surfaceRes, &obj->surfaceDestroy);
        wl_resource_set_implementation(fb, &cmFeedbackImpl, obj, cmFeedbackResourceDestroyed);
        srv->cmFeedbackResources.pushBack(obj);
    }

    void cmManagerCreateParamsCreator(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        wl_resource* pr = wl_resource_create(client, &wp_image_description_creator_params_v1_interface, wl_resource_get_version(res), id);

        if (!pr) {
            wl_client_post_no_memory(client);

            return;
        }

        CParams* p = srv->alloc->make<CParams>();

        *p = {};
        p->srv = srv;
        wl_resource_set_implementation(pr, &cmParamsImpl, p, cmParamsResourceDestroyed);
    }

    void cmIccCreate(wl_client* client, wl_resource* res, u32 id) {
        auto* icc = (CIcc*)wl_resource_get_user_data(res);

        if (!icc->fileSet) {
            wl_resource_post_error(
                res, WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_INCOMPLETE_SET,
                "an ICC profile file is required");

            return;
        }

        ColorDescription color;
        if (icc->readFailed) {
            cmMakeFailedImageDesc(icc->srv, client, wl_resource_get_version(res), id,
                                  WP_IMAGE_DESCRIPTION_V1_CAUSE_OPERATING_SYSTEM,
                                  "the ICC profile could not be read");
        } else if (!colorDescriptionFromIcc(icc->data.data(), icc->data.length(), color)) {
            cmMakeFailedImageDesc(icc->srv, client, wl_resource_get_version(res), id,
                                  WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
                                  "the ICC profile is invalid or unsupported");
        } else {
            CImgDesc description;

            description.color = color;
            cmMakeImageDesc(icc->srv, client, wl_resource_get_version(res), id,
                            description);
        }

        wl_resource_destroy(res);
    }

    void cmIccSetFile(wl_client*, wl_resource* res, int fd, u32 offset, u32 length) {
        auto* icc = (CIcc*)wl_resource_get_user_data(res);

        if (icc->fileSet) {
            close(fd);
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_ALREADY_SET,
                                   "the ICC profile file is already set");

            return;
        }

        if (!length || length > 32 * 1024 * 1024) {
            close(fd);
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_SIZE,
                                   "the ICC profile size must be between 1 byte and 32 MiB");

            return;
        }

        int flags = fcntl(fd, F_GETFL);
        struct stat st;
        bool readable = flags >= 0 && (flags & O_ACCMODE) != O_WRONLY;
        bool seekable = lseek(fd, 0, SEEK_CUR) >= 0;

        if (!readable || !seekable || fstat(fd, &st)) {
            close(fd);
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_FD,
                                   "the ICC profile fd must be readable and seekable");

            return;
        }

        if ((u64)offset + length > (u64)st.st_size) {
            close(fd);
            wl_resource_post_error(res, WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_OUT_OF_FILE,
                                   "the ICC profile range exceeds the file size");

            return;
        }

        icc->fileSet = true;
        icc->data.zero(length);
        size_t done = 0;

        while (done < length) {
            ssize_t n = pread(fd, icc->data.mutData() + done, length - done,
                              (off_t)offset + done);

            if (n > 0) {
                done += (size_t)n;
            } else if (n < 0 && errno == EINTR) {
                continue;
            } else {
                icc->readFailed = true;
                break;
            }
        }
        close(fd);
    }

    const struct wp_image_description_creator_icc_v1_interface cmIccImpl = {
        .create = cmIccCreate,
        .set_icc_file = cmIccSetFile,
    };

    void cmIccResourceDestroyed(wl_resource* res) {
        auto* icc = (CIcc*)wl_resource_get_user_data(res);

        if (icc && icc->srv) {
            icc->srv->alloc->release(icc);
        }
    }

    void cmManagerCreateIccCreator(wl_client* client, wl_resource* res, u32 id) {
        wl_resource* creator = wl_resource_create(
            client, &wp_image_description_creator_icc_v1_interface,
            wl_resource_get_version(res), id);

        if (!creator) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* srv = (WaylandImpl*)wl_resource_get_user_data(res);
        CIcc* icc = srv->alloc->make<CIcc>();

        icc->srv = srv;
        wl_resource_set_implementation(creator, &cmIccImpl, icc,
                                       cmIccResourceDestroyed);
    }

    void cmManagerCreateWindowsScrgb(wl_client* client, wl_resource* res, u32 id) {
        CImgDesc d;

        d.color = ColorDescription::extendedLinear();
        d.color.minNits = 0;
        d.color.maxNits = 10000.0;
        d.color.referenceNits = 203.0;
        d.color.linearOneNits = 80.0;
        d.color.targetMinNits = 0;
        d.color.targetMaxNits = 10000.0;
        cmMakeImageDesc((WaylandImpl*)wl_resource_get_user_data(res), client,
                        wl_resource_get_version(res), id, d);
    }

    void cmManagerGetImageDesc(wl_client* client, wl_resource* res, u32 id, wl_resource* reference) {
        (void)reference;
        cmMakeFailedImageDesc((WaylandImpl*)wl_resource_get_user_data(res), client,
                              wl_resource_get_version(res), id,
                              WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED,
                              "foreign image description references are unsupported");
    }

    void cmManagerCreateWindowsBt2100(wl_client* client, wl_resource* res, u32 id) {
        CImgDesc description;

        description.color = ColorDescription::bt2100Pq();
        cmMakeImageDesc((WaylandImpl*)wl_resource_get_user_data(res), client,
                        wl_resource_get_version(res), id, description);
    }

    const struct wp_color_manager_v1_interface cmManagerImpl = {
        .destroy = cmManagerDestroy,
        .get_output = cmManagerGetOutput,
        .get_surface = cmManagerGetSurface,
        .get_surface_feedback = cmManagerGetSurfaceFeedback,
        .create_icc_creator = cmManagerCreateIccCreator,
        .create_parametric_creator = cmManagerCreateParamsCreator,
        .create_windows_scrgb = cmManagerCreateWindowsScrgb,
        .get_image_description = cmManagerGetImageDesc,
        .create_windows_bt2100 = cmManagerCreateWindowsBt2100,
    };

    void colorManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_color_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &cmManagerImpl, data, nullptr);
        wp_color_manager_v1_send_supported_intent(res, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
        wp_color_manager_v1_send_supported_feature(res, WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC);
        wp_color_manager_v1_send_supported_feature(res, WP_COLOR_MANAGER_V1_FEATURE_ICC_V2_V4);
        wp_color_manager_v1_send_supported_feature(res, WP_COLOR_MANAGER_V1_FEATURE_SET_LUMINANCES);
        wp_color_manager_v1_send_supported_feature(res, WP_COLOR_MANAGER_V1_FEATURE_SET_PRIMARIES);
        wp_color_manager_v1_send_supported_feature(res, WP_COLOR_MANAGER_V1_FEATURE_SET_TF_POWER);
        wp_color_manager_v1_send_supported_feature(res, WP_COLOR_MANAGER_V1_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES);
        wp_color_manager_v1_send_supported_feature(res, WP_COLOR_MANAGER_V1_FEATURE_WINDOWS_SCRGB);
        if (version >= 3) {
            wp_color_manager_v1_send_supported_feature(
                res, WP_COLOR_MANAGER_V1_FEATURE_WINDOWS_BT2100);
        }
        if (version >= 2) {
            wp_color_manager_v1_send_supported_tf_named(
                res, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_COMPOUND_POWER_2_4);
        } else {
            wp_color_manager_v1_send_supported_tf_named(res, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
        }

        wp_color_manager_v1_send_supported_tf_named(res, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
        wp_color_manager_v1_send_supported_tf_named(res, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG);
        wp_color_manager_v1_send_supported_tf_named(res, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR);
        wp_color_manager_v1_send_supported_tf_named(res, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886);
        wp_color_manager_v1_send_supported_tf_named(res, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22);
        wp_color_manager_v1_send_supported_primaries_named(res, WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
        wp_color_manager_v1_send_supported_primaries_named(res, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);
        wp_color_manager_v1_send_supported_primaries_named(res, WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3);
        wp_color_manager_v1_send_supported_primaries_named(res, WP_COLOR_MANAGER_V1_PRIMARIES_PAL_M);
        wp_color_manager_v1_send_supported_primaries_named(res, WP_COLOR_MANAGER_V1_PRIMARIES_PAL);
        wp_color_manager_v1_send_supported_primaries_named(res, WP_COLOR_MANAGER_V1_PRIMARIES_NTSC);
        wp_color_manager_v1_send_supported_primaries_named(res, WP_COLOR_MANAGER_V1_PRIMARIES_GENERIC_FILM);
        wp_color_manager_v1_send_supported_primaries_named(res, WP_COLOR_MANAGER_V1_PRIMARIES_DCI_P3);
        wp_color_manager_v1_send_supported_primaries_named(res, WP_COLOR_MANAGER_V1_PRIMARIES_ADOBE_RGB);
        wp_color_manager_v1_send_done(res);
    }

    // ---- color-representation-v1 ----

    void representationDestroy(wl_client*, wl_resource* res) {
        if (auto* surface = (SurfaceImpl*)wl_resource_get_user_data(res)) {
            surface->pendRepresentation = {};
            surface->pendRepresentationChanged = true;
        }
        wl_resource_destroy(res);
    }

    void representationSetAlpha(wl_client*, wl_resource* res, u32 mode) {
        auto* surface = (SurfaceImpl*)wl_resource_get_user_data(res);
        if (!surface) {
            wl_resource_post_error(res, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_INERT,
                                   "the wl_surface is gone");
            return;
        }
        if (mode > WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT) {
            wl_resource_post_error(res, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_ALPHA_MODE,
                                   "unsupported alpha mode");
            return;
        }
        if (!surface->pendRepresentationChanged) {
            surface->pendRepresentation = surface->representation;
        }
        surface->pendRepresentation.alphaMode = mode;
        surface->pendRepresentationChanged = true;
    }

    void representationSetCoefficients(wl_client*, wl_resource* res, u32 coefficients,
                                       u32 range) {
        auto* surface = (SurfaceImpl*)wl_resource_get_user_data(res);
        if (!surface) {
            wl_resource_post_error(res, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_INERT,
                                   "the wl_surface is gone");
            return;
        }
        bool rgb = coefficients == WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY &&
                   range == WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL;
        bool yuv = (coefficients == WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709 ||
                    coefficients == WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601 ||
                    coefficients == WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020) &&
                   (range == WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL ||
                    range == WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED);
        if (!rgb && !yuv) {
            wl_resource_post_error(res, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_COEFFICIENTS,
                                   "unsupported coefficients and range combination");
            return;
        }
        if (!surface->pendRepresentationChanged) {
            surface->pendRepresentation = surface->representation;
        }
        surface->pendRepresentation.coefficients = coefficients;
        surface->pendRepresentation.range = range;
        surface->pendRepresentationChanged = true;
    }

    void representationSetChroma(wl_client*, wl_resource* res, u32 location) {
        auto* surface = (SurfaceImpl*)wl_resource_get_user_data(res);
        if (!surface) {
            wl_resource_post_error(res, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_INERT,
                                   "the wl_surface is gone");
            return;
        }
        if (location < WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_0 ||
            location > WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_5) {
            wl_resource_post_error(res, WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_CHROMA_LOCATION,
                                   "invalid chroma location");
            return;
        }
        if (!surface->pendRepresentationChanged) {
            surface->pendRepresentation = surface->representation;
        }
        surface->pendRepresentation.chromaLocation = location;
        surface->pendRepresentationChanged = true;
    }

    const struct wp_color_representation_surface_v1_interface representationImpl = {
        .destroy = representationDestroy,
        .set_alpha_mode = representationSetAlpha,
        .set_coefficients_and_range = representationSetCoefficients,
        .set_chroma_location = representationSetChroma,
    };

    void representationResourceDestroyed(wl_resource* res) {
        if (auto* surface = (SurfaceImpl*)wl_resource_get_user_data(res)) {
            surface->representationRes = nullptr;
        }
    }

    void representationManagerDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void representationManagerGetSurface(wl_client* client, wl_resource* res,
                                         u32 id, wl_resource* surfaceRes) {
        auto* surface = (SurfaceImpl*)wl_resource_get_user_data(surfaceRes);
        if (surface->representationRes) {
            wl_resource_post_error(res, WP_COLOR_REPRESENTATION_MANAGER_V1_ERROR_SURFACE_EXISTS,
                                   "surface already has a color representation object");
            return;
        }
        wl_resource* object = wl_resource_create(
            client, &wp_color_representation_surface_v1_interface, 1, id);
        if (!object) {
            wl_client_post_no_memory(client);
            return;
        }
        surface->representationRes = object;
        wl_resource_set_implementation(object, &representationImpl, surface,
                                       representationResourceDestroyed);
    }

    const struct wp_color_representation_manager_v1_interface representationManagerImpl = {
        .destroy = representationManagerDestroy,
        .get_surface = representationManagerGetSurface,
    };

    void representationManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(
            client, &wp_color_representation_manager_v1_interface, version, id);
        if (!res) {
            wl_client_post_no_memory(client);
            return;
        }
        wl_resource_set_implementation(res, &representationManagerImpl, data, nullptr);
        wp_color_representation_manager_v1_send_supported_alpha_mode(
            res, WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_ELECTRICAL);
        wp_color_representation_manager_v1_send_supported_alpha_mode(
            res, WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_OPTICAL);
        wp_color_representation_manager_v1_send_supported_alpha_mode(
            res, WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT);
        wp_color_representation_manager_v1_send_supported_coefficients_and_ranges(
            res, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY,
            WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL);
        constexpr u32 yuvCoefficients[] = {
            WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709,
            WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601,
            WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020,
        };
        for (u32 coefficients : yuvCoefficients) {
            wp_color_representation_manager_v1_send_supported_coefficients_and_ranges(
                res, coefficients, WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL);
            wp_color_representation_manager_v1_send_supported_coefficients_and_ranges(
                res, coefficients, WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED);
        }
        wp_color_representation_manager_v1_send_done(res);
    }


namespace {
    // the linked protocol XML caps what wl_global_create may advertise: a
    // build against a stale wayland-protocols must degrade to the XML's
    // version with a log line, and an outright creation failure is fatal --
    // silently dropping a global sends every client to a fast exit ("no
    // XDG shell interface")
    void global(wl_display* display, const wl_interface* iface, int version, void* data, wl_global_bind_func_t bind) {
        if (version > iface->version) {
            sysE << "imway: "_sv << StringView(iface->name) << " capped at v"_sv << (i64)iface->version
                 << " by the linked protocol XML (implemented v"_sv << (i64)version << ")"_sv << endL;
            version = iface->version;
        }

        STD_VERIFY(wl_global_create(display, iface, version, data, bind) != nullptr);
    }
}

void WaylandImpl::createGlobals() {
    global(display, &wl_compositor_interface, 7, this, compositorBind);
    global(display, &wl_subcompositor_interface, 1, this, subcompositorBind);
    global(display, &xdg_wm_base_interface, 7, this, wmBaseBind);
    global(display, &wl_output_interface, 4, this, outputBind);
    global(display, &wl_seat_interface, kSeatVersion, &seat, seatBind);
    global(display, &wl_data_device_manager_interface, 4, this, dataManagerBind);
    global(display, &zwp_primary_selection_device_manager_v1_interface, 1, this, primaryManagerBind);
    global(display, &wp_cursor_shape_manager_v1_interface, 2, this, cursorShapeManagerBind);
    global(display, &wp_single_pixel_buffer_manager_v1_interface, 1, this, spbManagerBind);
    global(display, &wp_presentation_interface, 2, this, presentationBind);
    global(display, &xdg_activation_v1_interface, 1, this, activationBind);
    global(display, &zxdg_decoration_manager_v1_interface, 1, this, decoManagerBind);
    global(display, &wp_viewporter_interface, 1, this, viewporterBind);
    global(display, &zxdg_output_manager_v1_interface, 3, this, xdgOutputManagerBind);
    global(display, &wp_fractional_scale_manager_v1_interface, 1, this, fracManagerBind);
    global(display, &wp_alpha_modifier_v1_interface, 1, this, alphaModManagerBind);
    global(display, &xdg_system_bell_v1_interface, 1, this, systemBellBind);
    global(display, &wp_content_type_manager_v1_interface, 1, this, contentTypeManagerBind);
    global(display, &wp_tearing_control_manager_v1_interface, 1, this, tearingManagerBind);
    global(display, &wp_fifo_manager_v1_interface, 1, this, fifoManagerBind);
    global(display, &wp_commit_timing_manager_v1_interface, 1, this, commitTimingManagerBind);
    global(display, &ext_output_image_capture_source_manager_v1_interface, 1, this, captureSourceManagerBind);
    global(display, &ext_image_copy_capture_manager_v1_interface, 1, this, captureManagerBind);
    global(display, &zwlr_screencopy_manager_v1_interface, 3, this, wlrCopyManagerBind);
    global(display, &ext_data_control_manager_v1_interface, 1, this, dcManagerBind);
    global(display, &zwp_text_input_manager_v3_interface, 1, this, textInputManagerBind);
    global(display, &zwp_input_method_manager_v2_interface, 1, this, imManagerBind);
    global(display, &zwp_virtual_keyboard_manager_v1_interface, 1, this, vkManagerBind);
    global(display, &wp_pointer_warp_v1_interface, 1, this, pointerWarpBind);
    global(display, &xdg_wm_dialog_v1_interface, 1, this, xdgWmDialogBind);
    global(display, &zwp_relative_pointer_manager_v1_interface, 1, &seat, relPointerManagerBind);
    global(display, &zwp_pointer_gestures_v1_interface, 3, &seat, pointerGesturesBind);
    global(display, &zwp_pointer_constraints_v1_interface, 1, this, pointerConstraintsBind);
    global(display, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1, this, kbInhibitManagerBind);
    global(display, &zwp_idle_inhibit_manager_v1_interface, 1, this, idleInhibitManagerBind);
    global(display, &xdg_toplevel_icon_manager_v1_interface, 1, this, iconManagerBind);
    global(display, &ext_idle_notifier_v1_interface, 2, this, idleNotifierBind);
    // Color-management: client electrical values are decoded into the linear
    // BT.2020 scene before composition and the output transform encodes that
    // scene for the active output description.
    global(display, &wp_color_manager_v1_interface, 3, this, colorManagerBind);
    global(display, &wp_color_representation_manager_v1_interface, 1, this,
                     representationManagerBind);

    u64 syncCap = 0;

    if (explicitSyncSupported && drmFd >= 0 && drmGetCap(drmFd, DRM_CAP_SYNCOBJ_TIMELINE, &syncCap) == 0 && syncCap) {
        global(display, &wp_linux_drm_syncobj_manager_v1_interface, 1, this, syncManagerBind);
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
            // v5 adds no new interface members over v4 (v6's
            // set_sampling_device is not in the installed wayland-protocols)
            dmabufVersion = 5;
        }

        global(display, &zwp_linux_dmabuf_v1_interface, dmabufVersion, this, dmabufBind);
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

void WaylandImpl::syncColorState() {
    OutputColorState color = output ? output->colorState() : OutputColorState::sdr();

    if (color == cmDisplayColor) {
        return;
    }

    cmDisplayColor = color;
    cmDisplayIdentity = ++cimgIdentity;

    for (CmOutput* obj : cmOutputResources) {
        if (output) {
            wp_color_management_output_v1_send_image_description_changed(obj->res);
        }
    }

    for (CmFeedback* obj : cmFeedbackResources) {
        if (!obj->surface) {
            continue;
        }

        if (wl_resource_get_version(obj->res) >= 2) {
            wp_color_management_surface_feedback_v1_send_preferred_changed2(
                obj->res, (u32)(cmDisplayIdentity >> 32), (u32)cmDisplayIdentity);
        } else {
            wp_color_management_surface_feedback_v1_send_preferred_changed(
                obj->res, (u32)cmDisplayIdentity);
        }
    }

    // color-management-output.changed is grouped with the core output batch.
    for (wl_resource* res : outputResources) {
        if (wl_resource_get_version(res) >= WL_OUTPUT_DONE_SINCE_VERSION) {
            wl_output_send_done(res);
        }
    }
}

void WaylandImpl::onListen(void* arg) {
    u32 msec = ((FrameEvent*)arg)->msec;

    syncColorState();
    syncKeyboardCapture();

    if (seat.kbFocus && (seat.kbFocus->minimized || !seat.kbFocus->mapped)) {
        seat.focusToplevel(nullptr);
    } else if (scene->focusedToplevel && !scene->focusedToplevel->minimized &&
        scene->focusedToplevel->mapped && scene->focusedToplevel != seat.kbFocus) {
        seat.focusToplevel(scene->focusedToplevel);
    }

    forEach<Surface, SceneNode>(scene->surfaces, [&](Surface& surface) {
        syncSurfaceOutputs((SurfaceImpl&)surface);
    });

    forEach<Toplevel>(scene->toplevels, [](Toplevel& tl) {
        auto& ti = (ToplevelImpl&)tl;

        if (ti.closeRequested) {
            ti.closeRequested = false;

            if (ti.res) {
                xdg_toplevel_send_close(ti.res);
            }
        }
    });

    forEach<Popup>(scene->popups, [&](Popup& value) {
        auto* popup = (PopupImpl*)&value;

        if (!popup->mapped || !popup->parent || !popup->xdg || !popup->positioner.reactive) {
            return;
        }

        int x = 0, y = 0, w = 0, h = 0;

        placePopup(*popup, popup->positioner, x, y, w, h);
        int compareX = popup->positionPending ? popup->pendingX : popup->x;
        int compareY = popup->positionPending ? popup->pendingY : popup->y;
        int compareW = popup->positionPending ? popup->pendingW : popup->w;
        int compareH = popup->positionPending ? popup->pendingH : popup->h;

        if (x != compareX || y != compareY || w != compareW || h != compareH) {
            configurePopupPosition(*popup, popup->positioner, false, 0);
        }
    });

    forEach<Toplevel>(scene->toplevels, [&](Toplevel& tl) {
        if (tl.mapped && !tl.minimized && tl.surface) {
            fireFrameCallbacks(*(SurfaceImpl*)tl.surface, msec);
        }
    });

    forEach<Popup>(scene->popups, [&](Popup& p) {
        if (p.mapped && p.surface) {
            fireFrameCallbacks(*(SurfaceImpl*)p.surface, msec);
        }
    });

    if (scene->dragIcon) {
        fireFrameCallbacks(*(SurfaceImpl*)scene->dragIcon, msec);
    }

    if (scene->cursorSurface) {
        fireFrameCallbacks(*(SurfaceImpl*)scene->cursorSurface, msec);
    }

    // wp-fifo: a presented surface clears its barrier and admits queued
    // updates; an invisible one cannot hold a barrier at all
    forEach<Surface, SceneNode>(scene->surfaces, [&](Surface& surface) {
        auto& s = (SurfaceImpl&)surface;

        if (!s.fifo || (s.fifo->queue.empty() && !s.fifo->barrier)) {
            return;
        }

        Surface* root = surface.rootSurface();
        bool shown = root == scene->dragIcon || root == scene->cursorSurface;

        if (!shown && root->toplevel) {
            shown = root->toplevel->mapped && !root->toplevel->minimized;
        } else if (!shown) {
            auto* rootImpl = (SurfaceImpl*)root;

            if (rootImpl->xdg && rootImpl->xdg->popup) {
                shown = rootImpl->xdg->popup->mapped;
            }
        }

        drainFifo(s, shown);

        if (!s.fifo->queue.empty()) {
            // a held entry needs future presentations to expire against
            scene->needsFrame = true;
        }
    });

    seat.imUpdatePopup();

    forEach<CaptureSession>(captureSessions, [&](CaptureSession& cs) {
        if (cs.frame && cs.frame->armed) {
            fulfillCapture(this, cs, *cs.frame);
        }
    });

    for (WlrCopyFrame* f : screencopyFrames) {
        if (f->armed) {
            fulfillWlrCopy(this, *f);
        }
    }

    for (CaptureCursorSession* cc : cursorSessions) {
        int x = (int)seat.curX, y = (int)seat.curY;

        if (!cc->entered) {
            cc->entered = true;
            ext_image_copy_capture_cursor_session_v1_send_enter(cc->res);
        }

        if (x != cc->lastX || y != cc->lastY) {
            cc->lastX = x;
            cc->lastY = y;
            ext_image_copy_capture_cursor_session_v1_send_position(cc->res, x, y);
        }

        if (scene->cursorHotX != cc->lastHotX || scene->cursorHotY != cc->lastHotY) {
            cc->lastHotX = scene->cursorHotX;
            cc->lastHotY = scene->cursorHotY;
            ext_image_copy_capture_cursor_session_v1_send_hotspot(cc->res, cc->lastHotX, cc->lastHotY);
        }
    }

    forEach<Toplevel>(scene->toplevels, [&](Toplevel& value) {
        auto* ti = (ToplevelImpl*)&value;

        if (!ti->mapped || !ti->surface || ti->desiredW <= 0) {
            return;
        }

        // judge desired against the geometry the renderer derived it from,
        // not live geometry: a commit racing the page flip must not read as
        // a fresh mismatch (see Toplevel::viewGeomW)
        bool differsView = ti->desiredW != ti->viewGeomW || ti->desiredH != ti->viewGeomH;
        bool differsSent = ti->desiredW != ti->cfgW || ti->desiredH != ti->cfgH;

        static const bool cfgTrace = getenv("IMWAY_CFG_TRACE") != nullptr;

        if (cfgTrace && (differsView || differsSent)) {
            sysO << "imway: cfg? desired="_sv << ti->desiredW << "x"_sv << ti->desiredH
                 << " view="_sv << ti->viewGeomW << "x"_sv << ti->viewGeomH
                 << " geom="_sv << ti->surface->geomW() << "x"_sv << ti->surface->geomH()
                 << " cfg="_sv << ti->cfgW << "x"_sv << ti->cfgH
                 << " ack="_sv << (int)(ti->xdg && (i32)(ti->xdg->committedAckSerial - ti->cfgSerial) >= 0)
                 << " docked="_sv << (int)ti->docked << (int)ti->cfgDocked
                 << " max="_sv << (int)ti->maximized << (int)ti->cfgMaximized << endL;
        }

        // one configure in flight: during an interactive resize the desired
        // size streams in every frame, but the next request waits until the
        // client answered the previous one — the window steps through
        // client-produced sizes only, at the client's own pace
        bool answered = ti->xdg && (i32)(ti->xdg->committedAckSerial - ti->cfgSerial) >= 0;

        // dock state changes alone need a configure: TILED comes and goes
        if ((differsView && differsSent && answered) || ti->docked != ti->cfgDocked ||
            ti->maximized != ti->cfgMaximized) {
            xdgToplevelConfigureSize(*ti, ti->desiredW, ti->desiredH);
        }
    });
}

void WaylandImpl::run() {
    ev_run(loop, 0);

    wl_display_destroy_clients(display);
    wl_display_destroy(display);
    display = nullptr;
}

// icon store reload: re-resolve every window still on a .desktop
// match; client-set icons are not ours to touch
void WaylandImpl::iconsReloaded() {
    forEach<Toplevel>(scene->toplevels, [&](Toplevel& tl) {
        if (!tl.iconFromClient) {
            tl.icon = icons->forAppId(sv(tl.appId));
        }
    });

    scene->needsFrame = true;
}

void WaylandImpl::sessionEnabled() {
}

void WaylandImpl::sessionDisabled() {
    seat.releaseAllKeys();
}

void WaylandImpl::syncKeyboardCapture() {
    bool cap = scene->kbCaptured;

    if (cap && !seat.uiCaptured) {
        seat.releaseAllKeys();
    }

    seat.uiCaptured = cap;
    seat.updateModifiers();
}

bool WaylandImpl::pointerMotion(PointerMotionEvent& ev) {
    activity();

    if (ev.kind == PointerMotionKind::relative) {
        seat.handleRelMotion(ev.dx, ev.dy, ev.dxRaw, ev.dyRaw);
    }

    if (ev.moved) {
        seat.handleMotion(ev.x, ev.y);
    }

    return true;
}

bool WaylandImpl::button(u32 btn, bool pressed) {
    activity();
    seat.handleButton(btn, pressed);

    return true;
}

bool WaylandImpl::key(u32 code, bool pressed) {
    activity();
    seat.handleKey(code, pressed);
    syncKeyboardCapture();

    return true;
}

bool WaylandImpl::scroll(const ScrollEvent& ev) {
    activity();
    seat.handleScroll(ev);

    return true;
}

bool WaylandImpl::swipeBegin(u32 fingers) {
    activity();
    seat.handleSwipeBegin(fingers);

    return true;
}

bool WaylandImpl::swipeUpdate(double dx, double dy) {
    seat.handleSwipeUpdate(dx, dy);

    return true;
}

bool WaylandImpl::swipeEnd(bool cancelled) {
    seat.handleSwipeEnd(cancelled);

    return true;
}

bool WaylandImpl::pinchBegin(u32 fingers) {
    activity();
    seat.handlePinchBegin(fingers);

    return true;
}

bool WaylandImpl::pinchUpdate(double dx, double dy, double scale, double rotation) {
    seat.handlePinchUpdate(dx, dy, scale, rotation);

    return true;
}

bool WaylandImpl::pinchEnd(bool cancelled) {
    seat.handlePinchEnd(cancelled);

    return true;
}

bool WaylandImpl::holdBegin(u32 fingers) {
    activity();
    seat.handleHoldBegin(fingers);

    return true;
}

bool WaylandImpl::holdEnd(bool cancelled) {
    seat.handleHoldEnd(cancelled);

    return true;
}

Wayland* Wayland::create(Composer& c, const WaylandConfig& cfg) {
    return c.pool->make<WaylandImpl>(c, cfg);
}
