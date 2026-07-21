#include "color.h"

#include <stdio.h>

int main() {
    ColorDescription sdr = ColorDescription::sRgb();
    ColorDescription pq = ColorDescription::bt2100Pq();
    ColorDescription hlg = ColorDescription::bt2100Hlg();
    ColorDescription linear = ColorDescription::extendedLinear();
    ColorDescription bt1886 = ColorDescription::bt1886();
    ColorDescription gamma22 = ColorDescription::gamma22();
    OutputColorState sdrOutput = OutputColorState::sdr();
    OutputColorState hdrOutput = OutputColorState::hdr10(203.0);

    if (sdr.managed() || sdr.hdr() || sdr.transfer != ColorTransfer::sRgb ||
        sdr.primaries != ColorPrimaries::sRgb || sdr.minNits != .2 ||
        sdr.maxNits != 80.0 || sdr.referenceNits != 80.0 ||
        !pq.managed() || !pq.hdr() || pq.transfer != ColorTransfer::pq ||
        pq.primaries != ColorPrimaries::bt2020 || pq.minNits != .005 ||
        pq.maxNits != 10000.0 || pq.referenceNits != 203.0 ||
        !hlg.managed() || !hlg.hdr() || hlg.transfer != ColorTransfer::hlg ||
        hlg.primaries != ColorPrimaries::bt2020 || hlg.minNits != .005 ||
        hlg.maxNits != 1000.0 || hlg.referenceNits != 203.0 ||
        !linear.managed() || linear.hdr() ||
        linear.transfer != ColorTransfer::extendedLinear ||
        linear.primaries != ColorPrimaries::sRgb || linear.minNits != .2 ||
        linear.maxNits != 80.0 || linear.referenceNits != 80.0 ||
        linear.linearOneNits != 80.0 || !bt1886.managed() || bt1886.hdr() ||
        bt1886.transfer != ColorTransfer::bt1886 || bt1886.minNits != .01 ||
        bt1886.maxNits != 100.0 || bt1886.referenceNits != 100.0 ||
        !gamma22.managed() || gamma22.hdr() ||
        gamma22.transfer != ColorTransfer::gamma22 || gamma22.minNits != .2 ||
        gamma22.maxNits != 80.0 || gamma22.referenceNits != 80.0) {
        fputs("bad standard color description\n", stderr);

        return 1;
    }

    if (sdrOutput.hdr() || sdrOutput.sdrWhiteNits != 80.0 ||
        sdrOutput.encoding != sdr || !hdrOutput.hdr() ||
        hdrOutput.sdrWhiteNits != 203.0 ||
        hdrOutput.encoding.transfer != pq.transfer ||
        hdrOutput.encoding.primaries != pq.primaries ||
        hdrOutput.encoding.targetMinNits != .0001 ||
        hdrOutput.encoding.targetMaxNits != 1000.0 ||
        hdrOutput.displayPeakNits != 1000.0 || hdrOutput.bpc != 10 ||
        hdrOutput == sdrOutput) {
        fputs("bad output color state\n", stderr);

        return 1;
    }

    puts("color-model: standard descriptions and output states ok");

    return 0;
}
