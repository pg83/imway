#include "scene.h"

int Surface::viewW() const {
    return vp.hasDst ? vp.dw : vp.hasSrc ? (int)vp.sw : width;
}

int Surface::viewH() const {
    return vp.hasDst ? vp.dh : vp.hasSrc ? (int)vp.sh : height;
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
