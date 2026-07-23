#include "color.h"

#include <math.h>
#include <stdio.h>

namespace {
    bool near(double a, double b, double epsilon = .01) {
        return fabs(a - b) < epsilon;
    }
}

int main() {
    OutputColorState output = OutputColorState::hdr10(203);

    output.displayMinNits = .005;
    output.displayPeakNits = 600;
    output.displayMaxFallNits = 350;
    output.encoding.target = Chromaticities::displayP3();
    output.encoding.targetMinNits = output.displayMinNits;
    output.encoding.targetMaxNits = output.displayPeakNits;

    HdrContentMetadata content;

    content.add(ColorDescription::sRgb(), output.sdrWhiteNits);

    ColorDescription movie = ColorDescription::bt2100Pq();

    movie.maxCll = 4000;
    movie.maxFall = 300;
    movie.maxCllSet = true;
    movie.maxFallSet = true;
    content.add(movie, output.sdrWhiteNits);

    HdrOutputMetadata metadata = hdrOutputMetadata(output, content);

    if (!(metadata.primaries == Chromaticities::displayP3()) || !near(metadata.minNits, .005) || !near(metadata.maxNits, 600) || metadata.maxCll != 599 || metadata.maxFall != 300) {
        fprintf(stderr, "mapped metadata mismatch: min %.4f max %.1f CLL %u FALL %u\n", metadata.minNits, metadata.maxNits, metadata.maxCll, metadata.maxFall);
        return 1;
    }

    HdrContentMetadata sdrOnly;

    sdrOnly.add(ColorDescription::sRgb(), output.sdrWhiteNits);
    metadata = hdrOutputMetadata(output, sdrOnly);

    if (metadata.maxCll != 203 || metadata.maxFall != 203) {
        fputs("SDR compositor content is missing from HDR metadata\n", stderr);
        return 1;
    }

    ColorDescription unknown = ColorDescription::bt2100Pq();
    HdrContentMetadata unknownContent;

    unknownContent.add(unknown, output.sdrWhiteNits);
    metadata = hdrOutputMetadata(output, unknownContent);

    if (metadata.maxCll != 600 || metadata.maxFall != 350 || metadata.maxFall > metadata.maxCll) {
        fputs("unknown HDR metadata does not conservatively use output volume\n", stderr);
        return 1;
    }

    puts("hdr-metadata: visible content follows mapped output volume");
    return 0;
}
