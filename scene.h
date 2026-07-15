#pragma once

#include <std/lib/vector.h>
#include <std/str/builder.h>
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
    i32 width = 0, height = 0;
    u32 format = 0;
    u64 modifier = 0;
    int nplanes = 0;
    int fds[kDmabufMaxPlanes] = {-1, -1, -1, -1};
    u32 offsets[kDmabufMaxPlanes] = {};
    u32 strides[kDmabufMaxPlanes] = {};
};

struct Surface {
    int width = 0, height = 0;
    int bufferScale = 1;
    bool hasContent = false;
    bool dirty = false;
    RectI damage;
    bool damageAll = false;
    stl::Vector<u8> pixels;
    DmabufBuffer* dmabuf = nullptr;

    SurfaceTexture* texture = nullptr;

    // explicit sync (linux-drm-syncobj): the renderer must wait this timeline
    // point before touching the current buffer
    u32 syncAcquireHandle = 0;
    u64 syncAcquirePoint = 0;
    bool syncAcquireWait = false;

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

    Subsurface* sub = nullptr;
    stl::Vector<Subsurface*> stackBelow;
    stl::Vector<Subsurface*> stackAbove;

    Toplevel* toplevel = nullptr;

    Surface* rootSurface();
    Toplevel* rootToplevel();
};

struct Subsurface {
    Surface* surface = nullptr;
    Surface* parent = nullptr;
    int x = 0, y = 0;
};

struct Toplevel {
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

    bool winSizeSet = false;
    int desiredW = 0, desiredH = 0;

    bool moveRequested = false;
    u32 resizeEdges = 0;

    // set by the ui (title bar close button), wayland turns it into
    // xdg_toplevel.close on the next shown frame
    bool closeRequested = false;

    bool fullscreen = false;
    bool activated = false;
    bool raiseRequested = false;
};

struct Popup {
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
    stl::Vector<Surface*> surfaces;
    stl::Vector<Toplevel*> toplevels;
    stl::Vector<Popup*> popups;

    stl::Vector<SurfaceTexture*> orphanedTextures;
    stl::Vector<DmabufBuffer*> deadDmabufs;

    int outW = 1280, outH = 800;
    double hz = 60.0;
    int framesDone = 0;

    bool needsFrame = true;
    bool drawCursor = false;

    // short active-layout name for the menu bar, written by wayland
    char layout[4] = "";

    const char* socketName = nullptr;

    // written by the renderer (imgui truth of the last frame), read by wayland
    Toplevel* focusedToplevel = nullptr;
    bool kbCaptured = false;
    bool ptrCaptured = false;

    // pointer-constraints state: written by wayland, honored by the input source
    bool pointerLocked = false;
    bool pointerConfined = false;
    double confineX0 = 0, confineY0 = 0, confineX1 = 0, confineY1 = 0;

    Surface* dragIcon = nullptr;

    CursorKind cursorShape = CursorKind::unset;
    Surface* cursorSurface = nullptr;
    int cursorHotX = 0, cursorHotY = 0;
};
