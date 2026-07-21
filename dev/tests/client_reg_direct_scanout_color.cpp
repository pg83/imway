#include "color.h"

#include <stdio.h>

int main() {
    if (!directScanoutColorCompatible(false, false) ||
        directScanoutColorCompatible(false, true) ||
        directScanoutColorCompatible(true, false) ||
        directScanoutColorCompatible(true, true)) {
        fputs("direct scanout accepted incompatible color state\n", stderr);

        return 1;
    }

    puts("direct-scanout-color: legacy SDR to SDR only");

    return 0;
}
