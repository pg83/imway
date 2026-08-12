// Unit test of the opaque-coverage policy behind ARGB direct scanout: only a
// surface whose declared opaque region fully covers the presented size may
// bypass composition on the non-blending primary plane.

#include "scene.h"

#include <stdio.h>

static int failures;

static void expect(bool value, const char* what) {
    if (!value) {
        fprintf(stderr, "opaque policy: %s\n", what);
        failures++;
    }
}

int main() {
    Surface s;

    s.width = 100;
    s.height = 50;

    expect(!s.opaqueCovers(), "no region must not count as opaque");

    s.opaqueRegionSet = true;
    expect(!s.opaqueCovers(), "empty region must not count as opaque");

    s.opaqueRegion.pushBack({0, 0, 100, 50});
    expect(s.opaqueCovers(), "exact full rect must cover");

    s.opaqueRegion.mut(0) = {0, 0, 100, 49};
    expect(!s.opaqueCovers(), "one missing row must not cover");

    s.opaqueRegion.mut(0) = {-10, -10, 200, 100};
    expect(s.opaqueCovers(), "oversized rect must cover");

    s.opaqueRegion.mut(0) = {1, 0, 100, 50};
    expect(!s.opaqueCovers(), "offset rect must not cover");

    // two half-rects jointly cover, but the policy is conservative
    s.opaqueRegion.mut(0) = {0, 0, 100, 25};
    s.opaqueRegion.pushBack({0, 25, 100, 25});
    expect(!s.opaqueCovers(), "joint tiling is not accepted by design");

    // the presented size follows the viewport, not the buffer
    s.opaqueRegion.clear();
    s.opaqueRegion.pushBack({0, 0, 50, 25});
    s.vp.hasDst = true;
    s.vp.dw = 50;
    s.vp.dh = 25;
    expect(s.opaqueCovers(), "viewport-sized rect must cover the viewport");

    if (failures) {
        return 1;
    }

    puts("scanout-opaque-policy: ok");
    return 0;
}
