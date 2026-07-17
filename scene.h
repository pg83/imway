#pragma once

#include "frame_resource.h"

#include <std/lib/list.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/types.h>

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

    ~DmabufBuffer() noexcept;
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
struct SceneNode: stl::IntrusiveNode {
};

struct GrabNode: stl::IntrusiveNode {
};

// SceneNode links it into Scene::surfaces, GrabNode into the seat's popup
// grab stack (self-linked while not grabbed)
struct Surface: SceneNode, GrabNode {
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

    SurfaceTexture* texture = nullptr;

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

    float imgX = 0, imgY = 0;
    bool hovered = false;

    // color-management-v1: the client's declared image description. hdr =
    // st2084 PQ + BT.2020, the passthrough case on an hdr output
    bool hdrContent = false;
    u32 hdrMaxCll = 0, hdrMaxLum = 0;

    // the individual transfer/gamut so the renderer can convert the surface
    // into the sRGB composition space. colorManaged = anything not already
    // plain sRGB, i.e. the surface needs a conversion pass.
    bool colorManaged = false;
    bool colorPq = false;    // st2084 PQ transfer (else sRGB)
    bool colorWide = false;  // BT.2020 primaries (else sRGB/709)
    u32 colorRefLum = 0;     // reference white in nits (0 = default)
    // bumped whenever the description changes, to invalidate a cached conversion
    u32 colorGeneration = 0;

    Subsurface* sub = nullptr;

    // z-piles of intrusively linked children (front = deepest); a child in
    // either list is reachable via its Subsurface node. The dying parent
    // clear()s both so orphaned children unlink among themselves safely.
    stl::IntrusiveList stackBelow;
    stl::IntrusiveList stackAbove;

    Toplevel* toplevel = nullptr;

    Surface* rootSurface();
    Toplevel* rootToplevel();
    bool contentMappedThroughAncestors() const;
};

// the node links it into the parent's stackBelow/stackAbove pile
struct Subsurface: stl::IntrusiveNode {
    Surface* surface = nullptr;
    Surface* parent = nullptr;
    int x = 0, y = 0;
};

// the node links it into Scene::toplevels
struct Toplevel: stl::IntrusiveNode {
    Surface* surface = nullptr;
    u64 id = 0;
    stl::StringBuilder title;
    stl::StringBuilder appId;
    bool mapped = false;

    // per-window keyboard layout, restored on focus
    u32 xkbGroup = 0;

    // true until the client accepts server-side decorations over
    // xdg-decoration; csd windows draw their own bar, we draw none
    bool csd = true;

    // window icon, resolved eagerly by wayland (on set_app_id and
    // xdg-toplevel-icon set_icon); store-owned unless iconFromClient, which
    // also shields it from app_id changes and icon store reloads
    Icon* icon = nullptr;
    bool iconFromClient = false;

    int desiredW = 0, desiredH = 0;
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
    bool activated = false;
    bool raiseRequested = false;

    // written by the renderer: the window sits in a dock node, so it must
    // fill its size exactly — wayland turns this into TILED states
    bool docked = false;
};

// the node links it into Scene::popups (insertion order = z-order)
struct Popup: stl::IntrusiveNode {
    Surface* surface = nullptr;
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
    double hz = 60.0;
    int framesDone = 0;

    bool needsFrame = true;
    bool drawCursor = false;

    // short active-layout name for the menu bar, written by wayland;
    // fixed 2-3 letter code, filled by the keyboard API
    char layout[4] = "";

    stl::StringView socketName;

    // written by the renderer (imgui truth of the last frame), read by wayland
    Toplevel* focusedToplevel = nullptr;
    bool kbCaptured = false;
    bool ptrCaptured = false;
    bool shortcutsInhibited = false;

    // pointer-constraints state: written by wayland, honored by the input source
    bool pointerLocked = false;
    bool pointerConfined = false;
    stl::Vector<RectI> confineRegion;

    Surface* dragIcon = nullptr;

    CursorKind cursorShape = CursorKind::unset;
    Surface* cursorSurface = nullptr;
    int cursorHotX = 0, cursorHotY = 0;
};
