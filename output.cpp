#include "output.h"

#include "settings.h"

OutputConfiguration outputConfiguration(const Settings& settings) {
    OutputConfiguration config;

    config.hdrSdrWhiteNits = settings.hdrEnabled() ? settings.sdrNits() : 0.;
    config.displayMinNits = settings.displayMinNits();
    config.displayPeakNits = settings.displayPeakNits();
    config.displayMaxFallNits = settings.displayMaxFallNits();
    config.bpc = settings.outputBpc();
    config.range = settings.outputRange();

    return config;
}
