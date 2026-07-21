#include "color.h"

#include <stdio.h>

int main() {
    OutputColorState sdr = OutputColorState::sdr();
    OutputColorState hdr = OutputColorState::hdr10(203.0);
    ColorDescription legacy = ColorDescription::sRgb();
    ColorDescription pq = ColorDescription::bt2100Pq();

    if (!directScanoutColorCompatible(sdr, legacy) ||
        directScanoutColorCompatible(sdr, pq) ||
        directScanoutColorCompatible(hdr, legacy) ||
        directScanoutColorCompatible(hdr, pq)) {
        fputs("direct scanout accepted incompatible color state\n", stderr);

        return 1;
    }

    puts("direct-scanout-color: legacy SDR to SDR only");

    return 0;
}
