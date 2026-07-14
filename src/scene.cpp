#include "scene.h"

int Surface::viewW() const {
    return vp.hasDst ? vp.dw : vp.hasSrc ? (int)vp.sw : width / bufferScale;
}

int Surface::viewH() const {
    return vp.hasDst ? vp.dh : vp.hasSrc ? (int)vp.sh : height / bufferScale;
}

bool Surface::inputContains(double sx, double sy) const {
    if (!inputRegionSet) {
        return true;
    }

    for (const RectI& r : inputRegion) {
        if (sx >= r.x && sy >= r.y && sx < r.x + r.w && sy < r.y + r.h) {
            return true;
        }
    }

    return false;
}

Surface* Surface::rootSurface() {
    Surface* s = this;

    while (s->sub && s->sub->parent) {
        s = s->sub->parent;
    }

    return s;
}

Toplevel* Surface::rootToplevel() {
    Surface* s = rootSurface();

    if (s->sub) {
        return nullptr;
    }

    return s->toplevel;
}

void unionRect(RectI& a, const RectI& b) {
    if (b.empty()) {
        return;
    }

    if (a.empty()) {
        a = b;

        return;
    }

    i32 x2 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    i32 y2 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;

    a.x = a.x < b.x ? a.x : b.x;
    a.y = a.y < b.y ? a.y : b.y;
    a.w = x2 - a.x;
    a.h = y2 - a.y;
}

void clipRect(RectI& r, i32 w, i32 h) {
    if (r.x < 0) {
        r.w += r.x;
        r.x = 0;
    }

    if (r.y < 0) {
        r.h += r.y;
        r.y = 0;
    }

    if (r.x + r.w > w) {
        r.w = w - r.x;
    }

    if (r.y + r.h > h) {
        r.h = h - r.y;
    }

    if (r.w < 0) {
        r.w = 0;
    }

    if (r.h < 0) {
        r.h = 0;
    }
}
