#pragma once

#include "color.h"
#include "frame_resource.h"
#include "weak_ptr.h"

#include <std/lib/list.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/types.h>

struct Composer;
struct Icon;
struct Popup;
struct Surface;
struct Toplevel;
struct Subsurface;
struct SurfaceTexture;

struct RectI {
    i32 x = 0, y = 0, w = 0, h = 0;

    bool empty() const {
        return w <= 0 || h <= 0;
    }
};

void unionRect(RectI& a, const RectI& b);
void clipRect(RectI& r, i32 w, i32 h);

struct DmabufFormat {
    u32 fourcc = 0;
    u64 modifier = 0;
};

inline constexpr int kDmabufMaxPlanes = 4;

struct DmabufBuffer {
    FrameResource* lifetime = nullptr;
    i32 width = 0, height = 0;
    u32 format = 0;
    u64 modifier = 0;
    int nplanes = 0;
    int fds[kDmabufMaxPlanes] = {-1, -1, -1, -1};
    u32 offsets[kDmabufMaxPlanes] = {};
    u32 strides[kDmabufMaxPlanes] = {};
    // KMS rejected this buffer for direct scanout for a permanent reason:
    // never retry the flip, composition carries it (mutter's taint)
    bool scanoutTainted = false;

    ~DmabufBuffer() noexcept;
};

struct ColorRepresentation {
    // color-representation-v1 wire values; zero coefficients/chroma means
    // that the client left the corresponding metadata unset.
    u32 alphaMode = 0;
    u32 coefficients = 0;
    u32 range = 0;
    u32 chromaLocation = 0;
};

inline void dmabufRef(DmabufBuffer* b) noexcept {
    if (b) {
        frameRef(b->lifetime);
    }
}

inline void dmabufUnref(DmabufBuffer* b) noexcept {
    if (b) {
        frameUnref(b->lifetime);
    }
}

// tagged list memberships: one entity can sit in several intrusive lists
// at once, the tag names which link to follow
struct SceneNode: stl::IntrusiveNode {};

struct GrabNode: stl::IntrusiveNode {};

// SceneNode links it into Scene::surfaces, GrabNode into the seat's popup
// grab stack (self-linked while not grabbed)
struct Surface: SceneNode, GrabNode {
    // weak-ring anchor: wayland seats it at creation and invalidate()s it in
    // the destroy handler; every Weak<Surface> observer nulls itself then
    Weak<Surface> weak;

    int width = 0, height = 0;
    int bufferScale = 1;
    int bufferTransform = 0;
    int bufferOffsetX = 0, bufferOffsetY = 0;
    bool hasContent = false;
    bool dirty = false;
    RectI damage;
    bool damageAll = false;
    stl::Vector<u8> pixels;
    DmabufBuffer* dmabuf = nullptr;
    FrameResource* frame = nullptr;

    // weak: the renderer invalidates the texture's anchor on destruction,
    // so a surface can never sample a freed texture
    Weak<SurfaceTexture> texture;

    // explicit sync (linux-drm-syncobj): the renderer must wait this timeline
    // point before touching the current buffer
    u32 syncAcquireHandle = 0;
    u64 syncAcquirePoint = 0;
    bool syncAcquireWait = false;
    bool explicitSync = false;

    struct {
        bool hasSrc = false, hasDst = false;
        double sx = 0, sy = 0, sw = 0, sh = 0;
        int dw = 0, dh = 0;
    } vp;

    int viewW() const;
    int viewH() const;

    // xdg window geometry: the visible rect within the surface (CSD shadows
    // and margins live outside of it); zero-initialized means "whole surface"
    RectI geom;
    bool hasGeom = false;

    int geomX() const;
    int geomY() const;
    int geomW() const;
    int geomH() const;

    bool inputRegionSet = false;
    stl::Vector<RectI> inputRegion;
    bool inputContains(double sx, double sy) const;

    // wl_surface.set_opaque_region, surface-local; consumed by the direct
    // scanout policy for alpha-capable formats
    bool opaqueRegionSet = false;
    stl::Vector<RectI> opaqueRegion;
    // one declared-opaque rect covers the whole presented surface
    bool opaqueCovers() const;

    // wp-alpha-modifier: a compositor-side opacity multiplier in [0,1]
    // applied on top of the buffer's own alpha (1 = unchanged)
    float alphaMult = 1.f;

    // wp-content-type hint (0 none, 1 photo, 2 video, 3 game); a scanout /
    // frame-pacing hint, not a correctness input
    u32 contentType = 0;

    // wp-tearing-control: the client allows tearing (async page flip) for
    // this surface's fullscreen presentation
    bool tearingAsync = false;

    float imgX = 0, imgY = 0;
    bool hovered = false;

    // Immutable color description committed with the current surface state.
    ColorDescription color;
    ColorRepresentation representation;

    // weak ring into the role object: nulls itself when the subsurface dies
    Weak<Subsurface> sub;

    // z-piles of intrusively linked children (front = deepest); a child in
    // either list is reachable via its Subsurface node. The dying parent
    // clear()s both so orphaned children unlink among themselves safely.
    stl::IntrusiveList stackBelow;
    stl::IntrusiveList stackAbove;

    // weak ring: the toplevel role, nulled when the toplevel (or the xdg
    // surface binding them) goes
    Weak<Toplevel> toplevel;

    Surface* rootSurface();
    Toplevel* rootToplevel();
    bool contentMappedThroughAncestors() const;
};

// the node links it into the parent's stackBelow/stackAbove pile
struct Subsurface: stl::IntrusiveNode {
    // weak-ring anchor, same contract as Surface::weak
    Weak<Subsurface> weak;

    // both weak: either surface dying nulls the pointer through the ring
    Weak<Surface> surface;
    Weak<Surface> parent;
    int x = 0, y = 0;
};

// the node links it into Scene::toplevels
struct Toplevel: stl::IntrusiveNode {
    // weak-ring anchor, same contract as Surface::weak
    Weak<Toplevel> weak;

    // weak ring into the wl_surface: nulled when the surface (or the xdg
    // surface binding them) goes
    Weak<Surface> surface;
    u64 id = 0;
    stl::StringBuilder title;
    stl::StringBuilder appId;
    // xdg-toplevel-tag: a stable machine token for WM rules and the
    // translated description that goes with it
    stl::StringBuilder tag;
    stl::StringBuilder tagDescription;

    // transient parent: xdg_toplevel.set_parent or an xdg-foreign import;
    // the ring clears it when the parent dies, an export revoke resets it
    Weak<Toplevel> parent;
    bool mapped = false;

    // dock MRU stamp from Scene::focusSeq, written where the renderer
    // already writes the focus truth; 0 = never focused, sorts to the
    // bottom in creation order
    u64 focusedAt = 0;

    // per-window keyboard layout, restored on focus
    u32 xkbGroup = 0;

    // ANR: the client misses two xdg pings — set by wayland, cleared by the
    // exact pong of the latest ping; the ui tints the title bar and turns
    // the close cross into a Terminate/Wait dialog
    bool unresponsive = false;

    // set by the ui (the Terminate choice); wayland kills the client by the
    // pid from wl_client_get_credentials on the next shown frame
    bool terminateRequested = false;

    // true until the client accepts server-side decorations over
    // xdg-decoration; csd windows draw their own bar, we draw none
    bool csd = true;

    // window icon inputs: nothing stores an Icon* — icon() resolves through
    // the provider registry at use time, by precomputed symbols only.
    // iconSym is the synthetic per-window key (the raw id bytes, hashed once
    // at creation) under which wayland serves client-pixel icons;
    // iconNameSym is the name a client chose via xdg-toplevel-icon (0 =
    // none); appIdSym is hash(lower(appId)), maintained by set_app_id
    u64 iconSym = 0;
    u64 iconNameSym = 0;
    u64 appIdSym = 0;

    // client pixels, then the chosen name, then the app_id .desktop match
    Icon* icon(Composer& c) const;

    int desiredW = 0, desiredH = 0;
    // the geometry snapshot desiredW/H was derived from. On KMS the frame
    // event — and with it the configure decision — fires on the page flip,
    // an ev iteration after the render, so client commits can land in
    // between; judging desired against live geometry there manufactures a
    // mismatch the renderer never saw and rings an endless configure/commit
    // oscillation with echo clients (zutty answers every configure with an
    // exact-size buffer)
    int viewGeomW = 0, viewGeomH = 0;
    int minW = 0, minH = 0;
    int maxW = 0, maxH = 0;

    // transactional resize (floating windows): the window is held at
    // applyW/H — the size derived from the last committed geometry, chrome
    // included — while dragW/H is where a border/grip drag wants it; the
    // drag only feeds configures, the window moves when the client answers
    // with a buffer. dragW/H written by the renderer's size-constraint
    // callback, consumed and cleared the same frame
    float applyW = 0, applyH = 0;
    float dragW = 0, dragH = 0;

    bool moveRequested = false;
    u32 resizeEdges = 0;
    u32 clientResizeEdges = 0;
    float resizeStartMouseX = 0, resizeStartMouseY = 0;
    float resizeStartW = 0, resizeStartH = 0;

    // interactive-resize position compensation (renderer): which edge/corner is
    // being dragged (bit0 left, bit1 top, bit7 active), the window's last known
    // top-left, and the last applied size — so when the size steps to a
    // client-committed buffer, a left/top drag grows toward the hand instead of
    // anchoring the top-left corner
    u8 resizeAnchor = 0;
    float curX = 0, curY = 0;
    float lastApplyW = 0, lastApplyH = 0;

    // set by the ui (title bar close button), wayland turns it into
    // xdg_toplevel.close on the next shown frame
    bool closeRequested = false;

    bool fullscreen = false;
    bool minimized = false;
    bool maximized = false;
    bool maximizedApplied = false;
    bool restoreRequested = false;
    float restoreX = 0, restoreY = 0;
    int restoreW = 0, restoreH = 0;
    bool activated = false;
    bool raiseRequested = false;

    // written by the renderer: the window sits in a dock node, so it must
    // fill its size exactly — wayland turns this into TILED states
    bool docked = false;

    // xdg-dialog: the client marked this toplevel as a modal dialog
    bool modal = false;
};

// the node links it into Scene::popups (insertion order = z-order)
struct Popup: stl::IntrusiveNode {
    // weak-ring anchor, same contract as Surface::weak
    Weak<Popup> weak;

    // weak ring into the popup's own surface. parent stays raw: the parent
    // death sweep must still see the link to dismiss the popup tree
    Weak<Surface> surface;
    Surface* parent = nullptr;
    int x = 0, y = 0;
    bool mapped = false;
    bool grab = false;
};

enum class CursorKind {
    unset,
    hidden,
    def,
    text,
    hand,
    grab,
    move,
    nsResize,
    ewResize,
    neswResize,
    nwseResize,
    notAllowed,
    wait,
};

struct Scene {
    stl::IntrusiveList surfaces;
    stl::IntrusiveList toplevels;
    stl::IntrusiveList popups;

    int outW = 1280, outH = 800;
    int workX = 0, workY = 0, workW = 1280, workH = 800;
    double hz = 60.0;
    int framesDone = 0;

    bool needsFrame = true;
    bool drawCursor = false;
    bool dockVisible = true;

    // xdg-system-bell: CLOCK_MONOTONIC ms of the last ring; the renderer
    // flashes the screen briefly while it is recent (0 = never rung).
    // bellCount is a monotone ring counter surfaced through the state dump.
    u64 bellMs = 0;
    u32 bellCount = 0;

    // short active-layout name for the menu bar, written by wayland;
    // fixed 2-3 letter code, filled by the keyboard API
    char layout[4] = "";

    stl::StringView socketName;

    // written by the renderer (imgui truth of the last frame), read by
    // wayland; weak because the client can destroy the toplevel between
    // buildUi and the frame event (KMS: render -> page flip)
    Weak<Toplevel> focusedToplevel;
    // monotonic focus counter behind Toplevel::focusedAt
    u64 focusSeq = 0;
    // toplevel id of the current direct scanout candidate (0 = none),
    // written by the renderer each frame; surfaced through the state dump
    u64 scanoutCandidateId = 0;
    // render faults attributed to a toplevel (by id): written by the
    // renderer when a client's texture cannot be built, drained by wayland
    // into a no_memory disconnect of the owner
    stl::Vector<u64> renderFaults;
    bool kbCaptured = false;
    bool ptrCaptured = false;
    bool shortcutsInhibited = false;

    // pointer-constraints state: written by wayland, honored by the input source
    bool pointerLocked = false;
    bool pointerConfined = false;
    stl::Vector<RectI> confineRegion;

    Weak<Surface> dragIcon;

    // xdg-toplevel-drag: the toplevel torn off during a data-source drag
    // follows the cursor at (renderer posX/posY - drag offset); null when
    // no window is attached. The renderer floats it out of docking while set.
    Weak<Toplevel> dragToplevel;
    int dragToplevelOffX = 0, dragToplevelOffY = 0;

    // input-method popup: the IME's candidate surface, positioned at the
    // active text input's cursor rectangle (screen coords, top-left)
    Weak<Surface> imePopup;
    float imePopupX = 0, imePopupY = 0;

    CursorKind cursorShape = CursorKind::unset;
    Surface* cursorSurface = nullptr;
    int cursorHotX = 0, cursorHotY = 0;
};
