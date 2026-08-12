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
    if (hdrOutput.sdrWhiteNits != 200.0 || hdrOutput.encoding.referenceNits != 200.0 || hdrOutput.hdrHeadroom() != 5.0) {
        fputs("bad HDR headroom policy\n", stderr);
        return 1;
    }
    hdrOutput.setSdrWhite(2000.0);
    if (hdrOutput.sdrWhiteNits != 1000.0 || hdrOutput.encoding.referenceNits != 1000.0 || hdrOutput.hdrHeadroom() != 1.0) {
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

    puts("color-model: standard descriptions and output states ok");

    return 0;
}
