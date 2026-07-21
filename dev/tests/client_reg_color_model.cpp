#include "color.h"

#include <stdio.h>

int main() {
    ColorDescription sdr = ColorDescription::sRgb();
    ColorDescription pq = ColorDescription::bt2100Pq();
    ColorDescription hlg = ColorDescription::bt2100Hlg();
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
        hlg.maxNits != 1000.0 || hlg.referenceNits != 203.0) {
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
