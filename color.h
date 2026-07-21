#pragma once

#include <std/sys/types.h>

enum class ColorTransfer {
    sRgb,
    pq,
    hlg,
    extendedLinear,
    bt1886,
    gamma22,
};

enum class ColorPrimaries {
    sRgb,
    bt2020,
    displayP3,
    custom,
};

struct Chromaticities {
    i32 rx = 0, ry = 0;
    i32 gx = 0, gy = 0;
    i32 bx = 0, by = 0;
    i32 wx = 0, wy = 0;

    static Chromaticities sRgb();
    static Chromaticities bt2020();
    static Chromaticities displayP3();
    bool operator==(const Chromaticities& other) const;
};

struct ColorDescription {
    ColorTransfer transfer = ColorTransfer::sRgb;
    ColorPrimaries primaries = ColorPrimaries::sRgb;
    Chromaticities primary = Chromaticities::sRgb();
    double minNits = .2;
    double maxNits = 80.0;
    double referenceNits = 80.0;
    Chromaticities target = Chromaticities::sRgb();
    double targetMinNits = .2;
    double targetMaxNits = 80.0;
    u32 maxCll = 0;
    u32 maxFall = 0;
    bool maxCllSet = false;
    bool maxFallSet = false;

    static ColorDescription sRgb();
    static ColorDescription bt2100Pq();
    bool managed() const;
    bool hdr() const;
    bool operator==(const ColorDescription& other) const;
    bool operator!=(const ColorDescription& other) const;
};

struct OutputColorState {
    ColorDescription encoding;
    double sdrWhiteNits = 80.0;
    double displayMinNits = .2;
    double displayPeakNits = 80.0;
    double displayMaxFallNits = 80.0;
    u32 bpc = 8;
    bool fullRange = true;

    static OutputColorState sdr();
    static OutputColorState hdr10(double sdrWhiteNits);
    bool hdr() const;
    bool operator==(const OutputColorState& other) const;
    bool operator!=(const OutputColorState& other) const;
};

bool directScanoutColorCompatible(const OutputColorState& output,
                                  const ColorDescription& surface);
