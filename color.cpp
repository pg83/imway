#include "color.h"

extern "C" {
#include <libdisplay-info/info.h>
}

#include <math.h>

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
           range == o.range;
}

bool parseEdidColorCapabilities(const void* data, size_t size,
                                DisplayColorCapabilities& capabilities) {
    capabilities = {};

    di_info* info = di_info_parse_edid(data, size);

    if (!info) {
        return false;
    }

    capabilities.valid = true;

    const di_hdr_static_metadata* hdr = di_info_get_hdr_static_metadata(info);
    const di_supported_signal_colorimetry* signal =
        di_info_get_supported_signal_colorimetry(info);
    const di_color_primaries* primaries = di_info_get_default_color_primaries(info);

    capabilities.pq = hdr->pq;
    capabilities.hlg = hdr->hlg;
    capabilities.bt2020Rgb = signal->bt2020_rgb;
    capabilities.minNits = hdr->desired_content_min_luminance;
    capabilities.peakNits = hdr->desired_content_max_luminance;
    capabilities.maxFallNits = hdr->desired_content_max_frame_avg_luminance;
    capabilities.hasPrimaries = primaries->has_primaries &&
                                primaries->has_default_white_point;

    if (capabilities.hasPrimaries) {
        auto scaled = [](float value) { return (i32)lround((double)value * 1000000.0); };

        capabilities.primaries = {
            scaled(primaries->primary[0].x), scaled(primaries->primary[0].y),
            scaled(primaries->primary[1].x), scaled(primaries->primary[1].y),
            scaled(primaries->primary[2].x), scaled(primaries->primary[2].y),
            scaled(primaries->default_white.x), scaled(primaries->default_white.y),
        };
    }

    di_info_destroy(info);

    return true;
}

OutputColorState outputColorState(const OutputConfiguration& config,
                                  const DisplayColorCapabilities& capabilities) {
    OutputColorState state = config.hdrSdrWhiteNits > 0 ?
        OutputColorState::hdr10(config.hdrSdrWhiteNits) : OutputColorState::sdr();

    if (state.hdr()) {
        if (capabilities.peakNits > 0) {
            state.displayPeakNits = capabilities.peakNits;
        }

        if (capabilities.maxFallNits > 0) {
            state.displayMaxFallNits = capabilities.maxFallNits;
        }

        if (capabilities.minNits > 0) {
            state.displayMinNits = capabilities.minNits;
        }

        if (capabilities.hasPrimaries) {
            state.encoding.target = capabilities.primaries;
        }
    }

    if (state.hdr()) {
        if (config.displayMinNits > 0) {
            state.displayMinNits = config.displayMinNits;
        }

        if (config.displayPeakNits > 0) {
            state.displayPeakNits = config.displayPeakNits;
        }

        if (config.displayMaxFallNits > 0) {
            state.displayMaxFallNits = config.displayMaxFallNits;
        }

        if (state.displayMaxFallNits > state.displayPeakNits) {
            state.displayMaxFallNits = state.displayPeakNits;
        }
    }

    state.bpc = config.bpc ? config.bpc : (state.hdr() ? 10 : 8);
    state.range = config.range;
    state.encoding.targetMinNits = state.displayMinNits;
    state.encoding.targetMaxNits = state.displayPeakNits;

    return state;
}

bool OutputColorState::operator!=(const OutputColorState& o) const {
    return !(*this == o);
}

bool directScanoutColorCompatible(const OutputColorState& output,
                                  const ColorDescription& surface) {
    return !output.hdr() && !surface.managed();
}
