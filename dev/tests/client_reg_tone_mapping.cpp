#include "color.h"

#include <math.h>
#include <stdio.h>

namespace {
    bool near(double a, double b, double epsilon = .01) {
        return fabs(a - b) < epsilon;
    }

    bool inTargetGamut(const OutputMapping& mapping, const ColorRgb& color,
                       double peak) {
        ColorRgb target = mapping.toTarget.apply(color);
        return target.r >= -.001 && target.g >= -.001 && target.b >= -.001 &&
               target.r <= peak + .001 && target.g <= peak + .001 &&
               target.b <= peak + .001;
    }
}

int main() {
    OutputColorState hdr = OutputColorState::hdr10(203);

    hdr.displayPeakNits = 600;
    hdr.displayMaxFallNits = 300;
    hdr.encoding.target = Chromaticities::displayP3();
    hdr.encoding.targetMaxNits = 600;

    OutputMapping mapping = outputMapping(hdr);
    ColorRgb anchor = mapOutputNits(mapping, {100, 100, 100});

    if (!near(anchor.r, 100) || !near(anchor.g, 100) || !near(anchor.b, 100)) {
        fputs("SDR anchor moved on HDR output\n", stderr);
        return 1;
    }

    double previous = 0;
    const double samples[] = {0, 100, 203, 400, 600, 1000, 4000, 10000};

    for (double sample : samples) {
        ColorRgb mapped = mapOutputNits(mapping, {sample, sample, sample});

        if (mapped.r + .001 < previous || mapped.r > 600.001 ||
            !near(mapped.r, mapped.g) || !near(mapped.g, mapped.b)) {
            fputs("tone curve is not neutral, monotonic and bounded\n", stderr);
            return 1;
        }

        previous = mapped.r;
    }

    ColorRgb highlight = mapOutputNits(mapping, {1000, 1000, 1000});

    if (highlight.r < 580 || highlight.r >= 600) {
        fputs("highlight shoulder wastes display headroom\n", stderr);
        return 1;
    }

    const ColorRgb wideSamples[] = {
        {0, 1000, 0}, {1000, 0, 0}, {0, 0, 1000}, {-20, 700, 120},
    };

    for (ColorRgb sample : wideSamples) {
        ColorRgb mapped = mapOutputNits(mapping, sample);

        if (!inTargetGamut(mapping, mapped, 600)) {
            fputs("wide-gamut sample escaped target volume\n", stderr);
            return 1;
        }
    }

    OutputColorState sdr = OutputColorState::sdr();
    OutputMapping sdrMapping = outputMapping(sdr);
    ColorRgb sdrHighlight = mapOutputNits(sdrMapping, {1000, 1000, 1000});

    if (sdrHighlight.r < 190 || sdrHighlight.r > 203.001 ||
        !inTargetGamut(sdrMapping, sdrHighlight, 203)) {
        fputs("HDR to SDR mapping is missing\n", stderr);
        return 1;
    }

    puts("tone-mapping: anchors, highlights, and target gamut ok");
    return 0;
}
