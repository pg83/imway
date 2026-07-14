#pragma once

#include <std/lib/vector.h>
#include <std/sys/types.h>

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
    bool hasContent = false;
    bool dirty = false;
    RectI damage;
    bool damageAll = false;
    stl::Vector<u8> pixels;
    DmabufBuffer* dmabuf = nullptr;

    SurfaceTexture* texture = nullptr;

    struct {
        bool hasSrc = false, hasDst = false;
        double sx = 0, sy = 0, sw = 0, sh = 0;
        int dw = 0, dh = 0;
    } vp;

    int viewW() const;
    int viewH() const;

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
    char title[256] = "(без имени)";
    char appId[128] = "";
    bool mapped = false;

    bool winSizeSet = false;
    int desiredW = 0, desiredH = 0;
};

struct Popup {
    Surface* surface = nullptr;
    Surface* parent = nullptr;
    int x = 0, y = 0;
    bool mapped = false;
    bool grab = false;
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

    Surface* dragIcon = nullptr;
};
