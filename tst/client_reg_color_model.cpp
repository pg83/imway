#include "color.h"

#include <stdio.h>

int main() {
    ColorDescription sdr = ColorDescription::sRgb();
    ColorDescription pq = ColorDescription::bt2100Pq();
    ColorDescription hlg = ColorDescription::bt2100Hlg();
    ColorDescription linear = ColorDescription::extendedLinear();
    ColorDescription bt1886 = ColorDescription::bt1886();
    ColorDescription gamma22 = ColorDescription::gamma22();
    ColorMatrix p3To2020 = colorPrimariesTransform(Chromaticities::displayP3(), Chromaticities::bt2020());
    OutputColorState sdrOutput = OutputColorState::sdr();
    OutputColorState hdrOutput = OutputColorState::hdr10(203.0);
    OutputMapping neutralMapping = outputMapping(hdrOutput);
    OutputMapping warmMapping = outputMapping(hdrOutput, 3400.0);

    if (sdr.managed() || sdr.hdr() || sdr.transfer != ColorTransfer::sRgb || sdr.primaries != ColorPrimaries::sRgb || sdr.minNits != .2 || sdr.maxNits != 80.0 || sdr.referenceNits != 80.0 || !pq.managed() || !pq.hdr() || pq.transfer != ColorTransfer::pq || pq.primaries != ColorPrimaries::bt2020 || pq.minNits != .005 || pq.maxNits != 10000.0 || pq.referenceNits != 203.0 || !hlg.managed() || !hlg.hdr() || hlg.transfer != ColorTransfer::hlg || hlg.primaries != ColorPrimaries::bt2020 || hlg.minNits != .005 || hlg.maxNits != 1000.0 || hlg.referenceNits != 203.0 || !linear.managed() || linear.hdr() || linear.transfer != ColorTransfer::extendedLinear || linear.primaries != ColorPrimaries::sRgb || linear.minNits != .2 || linear.maxNits != 80.0 || linear.referenceNits != 80.0 || linear.linearOneNits != 80.0 || !bt1886.managed() || bt1886.hdr() || bt1886.transfer != ColorTransfer::bt1886 || bt1886.minNits != .01 || bt1886.maxNits != 100.0 || bt1886.referenceNits != 100.0 || !gamma22.managed() || gamma22.hdr() || gamma22.transfer != ColorTransfer::gamma22 || gamma22.minNits != .2 || gamma22.maxNits != 80.0 || gamma22.referenceNits != 80.0) {
        fputs("bad standard color description\n", stderr);

        return 1;
    }

    ColorRgb p3Red = p3To2020.apply({1, 0, 0});
    ColorRgb p3White = p3To2020.apply({1, 1, 1});
    if (p3Red.r < .7 || p3Red.r > .8 || p3Red.g < .03 || p3Red.g > .07 || p3Red.b < -.02 || p3Red.b > .01 || p3White.r < .999 || p3White.r > 1.001 || p3White.g < .999 || p3White.g > 1.001 || p3White.b < .999 || p3White.b > 1.001) {
        fputs("bad Display P3 to BT.2020 matrix\n", stderr);
        return 1;
    }

    if (sdrOutput.hdr() || sdrOutput.sdrWhiteNits != 80.0 || sdrOutput.encoding != sdr || !hdrOutput.hdr() || hdrOutput.sdrWhiteNits != 203.0 || hdrOutput.encoding.transfer != pq.transfer || hdrOutput.encoding.primaries != pq.primaries || hdrOutput.encoding.targetMinNits != .0001 || hdrOutput.encoding.targetMaxNits != 1000.0 || hdrOutput.displayPeakNits != 1000.0 || hdrOutput.bpc != 10 || hdrOutput == sdrOutput) {
        fputs("bad output color state\n", stderr);

        return 1;
    }

    hdrOutput.setSdrWhite(200.0);
    if (hdrOutput.sdrWhiteNits != 200.0 || hdrOutput.encoding.referenceNits != 200.0) {
        fputs("bad HDR headroom policy\n", stderr);
        return 1;
    }
    hdrOutput.setSdrWhite(2000.0);
    if (hdrOutput.sdrWhiteNits != 1000.0 || hdrOutput.encoding.referenceNits != 1000.0) {
        fputs("SDR white exceeds calibrated HDR peak\n", stderr);
        return 1;
    }

    ColorRgb neutralWhite = neutralMapping.toTarget.apply({100, 100, 100});
    ColorRgb warmWhite = warmMapping.toTarget.apply({100, 100, 100});
    double warmY = warmMapping.targetLuma.r * warmWhite.r + warmMapping.targetLuma.g * warmWhite.g + warmMapping.targetLuma.b * warmWhite.b;

    if (neutralWhite.r < 99.999 || neutralWhite.r > 100.001 || neutralWhite.g < 99.999 || neutralWhite.g > 100.001 || neutralWhite.b < 99.999 || neutralWhite.b > 100.001 || warmWhite.r <= warmWhite.g || warmWhite.g <= warmWhite.b || warmY < 99.999 || warmY > 100.001) {
        fputs("bad Bradford night-light adaptation\n", stderr);

        return 1;
    }

    // equality compares every field: one field changed is a different value
    {
        int differs = 0, fields = 0;
        auto check = [&](bool equal) {
            fields++;
            differs += !equal;
        };
        Chromaticities base = Chromaticities::bt2020();
        i32 Chromaticities::*chroma[] = {&Chromaticities::rx, &Chromaticities::ry, &Chromaticities::gx, &Chromaticities::gy, &Chromaticities::bx, &Chromaticities::by, &Chromaticities::wx, &Chromaticities::wy};

        for (auto field : chroma) {
            Chromaticities c = base;

            c.*field += 1;
            check(c == base);
        }

        ColorDescription d = ColorDescription::bt2100Pq();
        auto desc = [&](auto change) {
            ColorDescription c = d;

            change(c);
            check(c == d);
        };

        desc([](ColorDescription& c) { c.transfer = ColorTransfer::hlg; });
        desc([](ColorDescription& c) { c.primaries = ColorPrimaries::custom; });
        desc([](ColorDescription& c) { c.primary.rx++; });
        desc([](ColorDescription& c) { c.minNits = 1; });
        desc([](ColorDescription& c) { c.maxNits = 1; });
        desc([](ColorDescription& c) { c.referenceNits = 1; });
        desc([](ColorDescription& c) { c.linearOneNits = 1; });
        desc([](ColorDescription& c) { c.target.wy++; });
        desc([](ColorDescription& c) { c.targetMinNits = 1; });
        desc([](ColorDescription& c) { c.targetMaxNits = 1; });
        desc([](ColorDescription& c) { c.maxCll = 1; });
        desc([](ColorDescription& c) { c.maxFall = 1; });
        desc([](ColorDescription& c) { c.maxCllSet = true; });
        desc([](ColorDescription& c) { c.maxFallSet = true; });
        desc([](ColorDescription& c) { c.directToBt2020 = true; });
        desc([](ColorDescription& c) { c.toBt2020[4] = 2; });
        desc([](ColorDescription& c) { c.gamma[1] = 2.2; });

        OutputColorState o = OutputColorState::hdr10(203.0);
        auto out = [&](auto change) {
            OutputColorState c = o;

            change(c);
            check(c == o);
        };

        out([](OutputColorState& c) { c.encoding.maxCll = 5; });
        out([](OutputColorState& c) { c.sdrWhiteNits = 1; });
        out([](OutputColorState& c) { c.displayMinNits = 1; });
        out([](OutputColorState& c) { c.displayPeakNits = 1; });
        out([](OutputColorState& c) { c.displayMaxFallNits = 1; });
        out([](OutputColorState& c) { c.bpc = 12; });
        out([](OutputColorState& c) { c.range = OutputRange::limited; });

        HdrOutputMetadata m;
        auto meta = [&](auto change) {
            HdrOutputMetadata c = m;

            change(c);
            check(c == m);
        };

        meta([](HdrOutputMetadata& c) { c.primaries.gx++; });
        meta([](HdrOutputMetadata& c) { c.minNits = 1; });
        meta([](HdrOutputMetadata& c) { c.maxNits = 1; });
        meta([](HdrOutputMetadata& c) { c.maxCll = 1; });
        meta([](HdrOutputMetadata& c) { c.maxFall = 1; });
        meta([](HdrOutputMetadata& c) { c.hdr = true; });

        if (differs != fields) {
            fprintf(stderr, "%d of %d single-field changes compared equal\n", fields - differs, fields);

            return 1;
        }
    }

    // an sRGB transfer is still managed with other primaries or the direct
    // BT.2020 path
    {
        ColorDescription p3 = ColorDescription::sRgb();
        ColorDescription direct = ColorDescription::sRgb();

        p3.primaries = ColorPrimaries::displayP3;
        direct.directToBt2020 = true;

        if (!p3.managed() || !direct.managed()) {
            fputs("an sRGB transfer with other primaries or the direct path reads as unmanaged\n", stderr);

            return 1;
        }
    }

    // the SDR white setter ignores a non-positive value
    {
        OutputColorState h = OutputColorState::hdr10(203.0);

        h.setSdrWhite(0);
        h.setSdrWhite(-5);

        if (h.sdrWhiteNits != 203.0) {
            fputs("a non-positive SDR white was taken\n", stderr);

            return 1;
        }
    }

    // a configured maxFALL above the peak is capped to the peak
    {
        OutputConfiguration config;
        DisplayColorCapabilities caps;

        config.hdrSdrWhiteNits = 203.0;
        config.displayPeakNits = 600.0;
        config.displayMaxFallNits = 900.0;

        OutputColorState s = outputColorState(config, caps);

        if (s.displayMaxFallNits != 600.0) {
            fprintf(stderr, "maxFALL %f was not capped to the 600-nit peak\n", s.displayMaxFallNits);

            return 1;
        }
    }

    // an HDR state without a peak falls back to 1000 nits, and a mapping
    // that knows no brighter output clamps at its peak
    {
        OutputColorState h = OutputColorState::hdr10(203.0);

        h.displayPeakNits = 0;

        OutputMapping mapping = outputMapping(h);

        if (mapping.peakNits != 1000.0) {
            fprintf(stderr, "an HDR state without a peak mapped to %f nits\n", mapping.peakNits);

            return 1;
        }
    }

    // night light: no temperature is no adaptation, a temperature below
    // 2222 K takes the low-temperature locus and warms even more, and one
    // above 4000 K takes the high-temperature locus and warms less
    {
        OutputMapping off = outputMapping(OutputColorState::sdr(), 0);
        OutputMapping candle = outputMapping(OutputColorState::sdr(), 2000.0);
        OutputMapping warm = outputMapping(OutputColorState::sdr(), 3400.0);
        OutputMapping mild = outputMapping(OutputColorState::sdr(), 5000.0);
        ColorRgb offWhite = off.toTarget.apply({1, 1, 1});
        ColorRgb candleWhite = candle.toTarget.apply({1, 1, 1});
        ColorRgb warmWhite = warm.toTarget.apply({1, 1, 1});
        ColorRgb mildWhite = mild.toTarget.apply({1, 1, 1});

        if (offWhite.r < .999 || offWhite.r > 1.001 || offWhite.b < .999 || offWhite.b > 1.001 || candleWhite.b >= warmWhite.b || mildWhite.b <= warmWhite.b || mildWhite.b >= offWhite.b) {
            fputs("bad night-light temperatures\n", stderr);

            return 1;
        }
    }

    // a BT.2020 green outside the sRGB gamut maps to a colour with no
    // negative channel: the chroma scales back toward the luminance
    {
        OutputMapping sdrMapping = outputMapping(OutputColorState::sdr());
        ColorRgb mapped = mapOutputNits(sdrMapping, {0, 100, 0});
        ColorRgb inTarget = sdrMapping.toTarget.apply(mapped);

        if (inTarget.r < -1e-6 || inTarget.g < -1e-6 || inTarget.b < -1e-6) {
            fprintf(stderr, "an out-of-gamut green kept a negative channel: %f %f %f\n", inTarget.r, inTarget.g, inTarget.b);

            return 1;
        }
    }

    // the brightest value per transfer
    {
        ColorDescription icc = ColorDescription::gamma22();
        ColorDescription noReference = ColorDescription::gamma22();

        icc.transfer = ColorTransfer::iccGamma;
        noReference.referenceNits = 0;

        if (surfaceMaxNits(ColorDescription::sRgb(), 203) != 203 || surfaceMaxNits(ColorDescription::bt2100Pq(), 203) != 10000 || surfaceMaxNits(ColorDescription::bt2100Hlg(), 203) != 1000 || surfaceMaxNits(ColorDescription::extendedLinear(), 203) < 1e8 || surfaceMaxNits(ColorDescription::bt1886(), 203) != 100 || surfaceMaxNits(icc, 203) != 80 || surfaceMaxNits(noReference, 203) != 203) {
            fputs("bad per-transfer brightest value\n", stderr);

            return 1;
        }
    }

    // chromaticities: a zero y anywhere, or primaries on one line, make no
    // colour space
    {
        Chromaticities greenless = Chromaticities::bt2020();
        Chromaticities blueless = Chromaticities::bt2020();
        Chromaticities collinear = {100000, 100000, 200000, 200000, 300000, 300000, 312700, 329000};

        greenless.gy = 0;
        blueless.by = 0;

        if (!Chromaticities::bt2020().valid() || greenless.valid() || blueless.valid() || collinear.valid()) {
            fputs("bad chromaticity validation\n", stderr);

            return 1;
        }
    }

    puts("color-model: standard descriptions and output states ok");

    return 0;
}
