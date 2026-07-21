#include "color.h"

Chromaticities Chromaticities::sRgb() {
    return {640000, 330000, 300000, 600000, 150000, 60000, 312700, 329000};
}

Chromaticities Chromaticities::bt2020() {
    return {708000, 292000, 170000, 797000, 131000, 46000, 312700, 329000};
}

Chromaticities Chromaticities::displayP3() {
    return {680000, 320000, 265000, 690000, 150000, 60000, 312700, 329000};
}

bool Chromaticities::operator==(const Chromaticities& o) const {
    return rx == o.rx && ry == o.ry && gx == o.gx && gy == o.gy &&
           bx == o.bx && by == o.by && wx == o.wx && wy == o.wy;
}

ColorDescription ColorDescription::sRgb() {
    return {};
}

ColorDescription ColorDescription::bt2100Pq() {
    ColorDescription d;

    d.transfer = ColorTransfer::pq;
    d.primaries = ColorPrimaries::bt2020;
    d.primary = Chromaticities::bt2020();
    d.minNits = .005;
    d.maxNits = 10000.0;
    d.referenceNits = 203.0;
    d.target = d.primary;
    d.targetMinNits = d.minNits;
    d.targetMaxNits = d.maxNits;

    return d;
}

bool ColorDescription::managed() const {
    return transfer != ColorTransfer::sRgb || primaries != ColorPrimaries::sRgb;
}

bool ColorDescription::hdr() const {
    return transfer == ColorTransfer::pq || transfer == ColorTransfer::hlg;
}

bool ColorDescription::operator==(const ColorDescription& o) const {
    return transfer == o.transfer && primaries == o.primaries && primary == o.primary &&
           minNits == o.minNits && maxNits == o.maxNits &&
           referenceNits == o.referenceNits && target == o.target &&
           targetMinNits == o.targetMinNits && targetMaxNits == o.targetMaxNits &&
           maxCll == o.maxCll && maxFall == o.maxFall &&
           maxCllSet == o.maxCllSet && maxFallSet == o.maxFallSet;
}

bool ColorDescription::operator!=(const ColorDescription& o) const {
    return !(*this == o);
}

OutputColorState OutputColorState::sdr() {
    return {};
}

OutputColorState OutputColorState::hdr10(double white) {
    OutputColorState state;

    state.encoding = ColorDescription::bt2100Pq();
    state.sdrWhiteNits = white;
    state.displayMinNits = .0001;
    state.displayPeakNits = 1000.0;
    state.displayMaxFallNits = 400.0;
    state.bpc = 10;
    state.encoding.referenceNits = white;
    state.encoding.targetMinNits = state.displayMinNits;
    state.encoding.targetMaxNits = state.displayPeakNits;

    return state;
}

bool OutputColorState::hdr() const {
    return encoding.hdr();
}

bool OutputColorState::operator==(const OutputColorState& o) const {
    return encoding == o.encoding && sdrWhiteNits == o.sdrWhiteNits &&
           displayMinNits == o.displayMinNits && displayPeakNits == o.displayPeakNits &&
           displayMaxFallNits == o.displayMaxFallNits && bpc == o.bpc &&
           fullRange == o.fullRange;
}

bool OutputColorState::operator!=(const OutputColorState& o) const {
    return !(*this == o);
}

bool directScanoutColorCompatible(const OutputColorState& output,
                                  const ColorDescription& surface) {
    return !output.hdr() && !surface.managed();
}
