// Theme palette invariants, standalone: deterministic derivation from the
// seeds, revision on edit, tonal ramps that actually ramp, channel sanity
// and the packed-color conventions.
#include "theme.h"

#include <math.h>
#include <stdio.h>

namespace {
    float luminance(const ThemeColor& c) {
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    }

    bool channelsSane(const ThemeColor& c) {
        return c.r >= 0.f && c.r <= 1.f && c.g >= 0.f && c.g <= 1.f && c.b >= 0.f && c.b <= 1.f && c.a >= 0.f && c.a <= 1.f;
    }

    bool sameColor(const ThemeColor& a, const ThemeColor& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    bool rampOk(const ThemePalette& p, const char* name) {
        float first = luminance(p[0]);
        float last = luminance(p[ThemePalette::toneCount - 1]);

        if (fabsf(last - first) < 0.4f) {
            fprintf(stderr, "%s ramp barely moves: %f -> %f\n", name, first, last);
            return false;
        }

        float dir = last > first ? 1.f : -1.f;

        for (int i = 0; i < ThemePalette::toneCount; i++) {
            if (!channelsSane(p[i])) {
                fprintf(stderr, "%s tone %d out of range\n", name, i);
                return false;
            }

            if (i && dir * (luminance(p[i]) - luminance(p[i - 1])) < -0.02f) {
                fprintf(stderr, "%s ramp not monotonic at tone %d\n", name, i);
                return false;
            }
        }

        return true;
    }
}

int main() {
    Theme a, b;

    // the same seeds derive the same desktop, bit for bit
    for (int i = 0; i < ThemePalette::toneCount; i++) {
        if (!sameColor(a.neutral[i], b.neutral[i]) || !sameColor(a.selection[i], b.selection[i]) || !sameColor(a.accentTones[i], b.accentTones[i])) {
            fprintf(stderr, "theme derivation is not deterministic (tone %d)\n", i);
            return 1;
        }
    }

    if (!rampOk(a.neutral, "neutral") || !rampOk(a.selection, "selection") || !rampOk(a.accentTones, "accent")) {
        return 1;
    }

    if (!channelsSane(a.accent) || !channelsSane(a.desktop)) {
        fprintf(stderr, "derived accent/desktop out of range\n");
        return 1;
    }

    u64 rev = a.revision;

    a.setSeeds(ThemeColor{0.6f, 0.2f, 0.2f, 1.f}, ThemeColor{0.2f, 0.6f, 0.3f, 1.f});

    if (a.revision == rev) {
        fprintf(stderr, "setSeeds did not bump the revision\n");
        return 1;
    }

    if (sameColor(a.accent, b.accent) && sameColor(a.desktop, b.desktop)) {
        fprintf(stderr, "new seeds did not change the derived colors\n");
        return 1;
    }

    if (themeColorU32(ThemeColor{1.f, 0.f, 0.f, 0.f}) != 0x000000ffu) {
        fprintf(stderr, "themeColorU32 does not pack R into the low byte\n");
        return 1;
    }

    ThemeColor faded = themeAlpha(ThemeColor{0.5f, 0.5f, 0.5f, 1.f}, 0.25f);

    if (faded.a != 0.25f || faded.r != 0.5f) {
        fprintf(stderr, "themeAlpha mangled the color\n");
        return 1;
    }

    return 0;
}
