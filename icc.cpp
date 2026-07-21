#include "icc.h"

#include <lcms2.h>

#include <math.h>

namespace {
    bool curveMatches(const cmsToneCurve& curve, double (*fn)(double)) {
        constexpr int samples = 256;
        constexpr double tolerance = 2e-4;

        for (int i = 0; i <= samples; i++) {
            double x = (double)i / samples;
            double actual = cmsEvalToneCurveFloat(&curve, (float)x);

            if (!isfinite(actual) || fabs(actual - fn(x)) > tolerance) {
                return false;
            }
        }

        return true;
    }

    double srgbCurve(double x) {
        return x <= .04045 ? x / 12.92 : pow((x + .055) / 1.055, 2.4);
    }

    double linearCurve(double x) {
        return x;
    }

    bool curveGamma(const cmsToneCurve& curve, double& gamma) {
        gamma = cmsEstimateGamma(&curve, 1e-5);

        if (!isfinite(gamma) || gamma < .1 || gamma > 10) {
            return false;
        }

        constexpr int samples = 256;
        constexpr double tolerance = 2e-4;

        for (int i = 0; i <= samples; i++) {
            double x = (double)i / samples;
            double actual = cmsEvalToneCurveFloat(&curve, (float)x);

            if (!isfinite(actual) || fabs(actual - pow(x, gamma)) > tolerance) {
                return false;
            }
        }

        return true;
    }

    cmsHPROFILE linearBt2020Profile() {
        cmsCIExyY white{0.3127, 0.3290, 1.0};
        cmsCIExyYTRIPLE primaries{
            {0.708, 0.292, 1.0},
            {0.170, 0.797, 1.0},
            {0.131, 0.046, 1.0},
        };
        cmsToneCurve* linear = cmsBuildGamma(nullptr, 1.0);
        cmsToneCurve* curves[3] = {linear, linear, linear};
        cmsHPROFILE profile = cmsCreateRGBProfile(&white, &primaries, curves);

        cmsFreeToneCurve(linear);
        return profile;
    }
}

bool colorDescriptionFromIcc(const void* data, size_t size, ColorDescription& out) {
    if (!data || !size || size > 0xffffffffu) {
        return false;
    }

    cmsHPROFILE input = cmsOpenProfileFromMem(data, (cmsUInt32Number)size);
    if (!input) {
        return false;
    }

    double version = cmsGetProfileVersion(input);
    cmsProfileClassSignature profileClass = cmsGetDeviceClass(input);
    bool valid = version >= 2 && version < 5 &&
        (profileClass == cmsSigDisplayClass || profileClass == cmsSigColorSpaceClass) &&
        cmsGetColorSpace(input) == cmsSigRgbData && cmsIsMatrixShaper(input);

    const cmsToneCurve* curves[3] = {
        (const cmsToneCurve*)cmsReadTag(input, cmsSigRedTRCTag),
        (const cmsToneCurve*)cmsReadTag(input, cmsSigGreenTRCTag),
        (const cmsToneCurve*)cmsReadTag(input, cmsSigBlueTRCTag),
    };

    valid &= curves[0] && curves[1] && curves[2];
    ColorDescription color;

    if (valid) {
        bool srgb = true;

        for (int i = 0; i < 3; i++) {
            srgb &= curveMatches(*curves[i], srgbCurve);
        }

        if (!srgb) {
            color.transfer = ColorTransfer::iccGamma;
            for (int i = 0; i < 3; i++) {
                if (curveMatches(*curves[i], linearCurve)) {
                    color.gamma[i] = 1;
                } else if (!curveGamma(*curves[i], color.gamma[i])) {
                    valid = false;
                }
            }
        }
    }

    cmsHPROFILE output = valid ? linearBt2020Profile() : nullptr;
    cmsHTRANSFORM transform = output ? cmsCreateTransform(
        input, TYPE_RGB_FLT, output, TYPE_RGB_FLT,
        INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE) : nullptr;

    if (!transform) {
        valid = false;
    }

    if (valid) {
        const float basis[12] = {
            0, 0, 0,
            1, 0, 0,
            0, 1, 0,
            0, 0, 1,
        };
        float mapped[12] = {};

        cmsDoTransform(transform, basis, mapped, 4);
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                double value = (double)mapped[(col + 1) * 3 + row] - mapped[row];

                if (!isfinite(value)) {
                    valid = false;
                }
                color.toBt2020[row * 3 + col] = value;
            }
        }
        color.directToBt2020 = true;
    }

    if (transform) {
        cmsDeleteTransform(transform);
    }
    if (output) {
        cmsCloseProfile(output);
    }
    cmsCloseProfile(input);

    if (valid) {
        out = color;
    }

    return valid;
}
