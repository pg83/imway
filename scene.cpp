#include "scene.h"

#include <unistd.h>

DmabufBuffer::~DmabufBuffer() noexcept {
    for (int& fd : fds) {
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }
}

int Surface::viewW() const {
    bool swapped = bufferTransform == 1 || bufferTransform == 3 || bufferTransform == 5 || bufferTransform == 7;

    return vp.hasDst ? vp.dw : vp.hasSrc ? (int)vp.sw : (swapped ? height : width) / bufferScale;
}

int Surface::viewH() const {
    bool swapped = bufferTransform == 1 || bufferTransform == 3 || bufferTransform == 5 || bufferTransform == 7;

    return vp.hasDst ? vp.dh : vp.hasSrc ? (int)vp.sh : (swapped ? width : height) / bufferScale;
}

int Surface::geomX() const {
    return hasGeom ? geom.x : 0;
}

int Surface::geomY() const {
    return hasGeom ? geom.y : 0;
}

int Surface::geomW() const {
    if (!hasGeom || geom.w <= 0) {
        return viewW();
    }

    int avail = viewW() - geom.x;

    return geom.w < avail ? geom.w : (avail > 0 ? avail : viewW());
}

int Surface::geomH() const {
    if (!hasGeom || geom.h <= 0) {
        return viewH();
    }

    int avail = viewH() - geom.y;

    return geom.h < avail ? geom.h : (avail > 0 ? avail : viewH());
}

bool Surface::opaqueCovers() const {
    if (!opaqueRegionSet) {
        return false;
    }

    // conservative: full coverage by a single rect (the common client shape);
    // multi-rect tilings that only jointly cover the surface do not count
    for (const RectI& r : opaqueRegion) {
        if (r.x <= 0 && r.y <= 0 && r.x + r.w >= viewW() && r.y + r.h >= viewH()) {
            return true;
        }
    }

    return false;
}

bool Surface::inputContains(double sx, double sy) const {
    if (!inputRegionSet) {
        return true;
    }

    for (const RectI& r : inputRegion) {
        if (sx >= r.x && sy >= r.y && sx < (double)r.x + r.w && sy < (double)r.y + r.h) {
            return true;
        }
    }

    return false;
}

Surface* Surface::rootSurface() {
    Surface* s = this;

    while (s->sub && s->sub->parent) {
        s = s->sub->parent.get();
    }

    return s;
}

Toplevel* Surface::rootToplevel() {
    Surface* s = rootSurface();

    if (s->sub) {
        return nullptr;
    }

    return s->toplevel.get();
}

bool Surface::contentMappedThroughAncestors() const {
    const Surface* surface = this;

    while (surface) {
        if (!surface->hasContent) {
            return false;
        }

        if (!surface->sub || !surface->sub->parent) {
            return true;
        }

        surface = surface->sub->parent.get();
    }

    return false;
}

void unionRect(RectI& a, const RectI& b) {
    if (b.empty()) {
        return;
    }

    if (a.empty()) {
        a = b;

        return;
    }

    i64 ax2 = (i64)a.x + a.w, ay2 = (i64)a.y + a.h;
    i64 bx2 = (i64)b.x + b.w, by2 = (i64)b.y + b.h;
    i64 x1 = a.x < b.x ? a.x : b.x;
    i64 y1 = a.y < b.y ? a.y : b.y;
    i64 x2 = ax2 > bx2 ? ax2 : bx2;
    i64 y2 = ay2 > by2 ? ay2 : by2;
    constexpr i64 maxI32 = 0x7fffffff;

    a.x = (i32)x1;
    a.y = (i32)y1;
    a.w = (i32)(x2 - x1 > maxI32 ? maxI32 : x2 - x1);
    a.h = (i32)(y2 - y1 > maxI32 ? maxI32 : y2 - y1);
}

void clipRect(RectI& r, i32 w, i32 h) {
    i64 x1 = r.x > 0 ? r.x : 0;
    i64 y1 = r.y > 0 ? r.y : 0;
    i64 x2 = (i64)r.x + r.w;
    i64 y2 = (i64)r.y + r.h;

    if (x2 > w) {
        x2 = w;
    }

    if (y2 > h) {
        y2 = h;
    }

    r.x = (i32)(x1 < w ? x1 : w);
    r.y = (i32)(y1 < h ? y1 : h);
    r.w = (i32)(x2 > x1 ? x2 - x1 : 0);
    r.h = (i32)(y2 > y1 ? y2 - y1 : 0);
}
