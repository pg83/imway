#include "theme.h"

#include <imgui.h>

#include <math.h>

namespace {
    struct Lch {
        float l = 0.f;
        float c = 0.f;
        float h = 0.f;
    };

    float clamp01(float v) {
        return v < 0.f ? 0.f : v > 1.f ? 1.f : v;
    }

    float srgbLinear(float v) {
        return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
    }

    float linearSrgb(float v) {
        return v <= 0.0031308f ? 12.92f * v : 1.055f * powf(v, 1.f / 2.4f) - 0.055f;
    }

    Lch toLch(ThemeColor color) {
        float r = srgbLinear(clamp01(color.r));
        float g = srgbLinear(clamp01(color.g));
        float b = srgbLinear(clamp01(color.b));
        float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
        float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
        float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

        l = cbrtf(l);
        m = cbrtf(m);
        s = cbrtf(s);

        float ll = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
        float a = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
        float bb = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;

        return {ll, sqrtf(a * a + bb * bb), atan2f(bb, a)};
    }

    bool fromLchRaw(Lch color, ThemeColor& out) {
        float a = color.c * cosf(color.h);
        float b = color.c * sinf(color.h);
        float l_ = color.l + 0.3963377774f * a + 0.2158037573f * b;
        float m_ = color.l - 0.1055613458f * a - 0.0638541728f * b;
        float s_ = color.l - 0.0894841775f * a - 1.2914855480f * b;
        float l = l_ * l_ * l_;
        float m = m_ * m_ * m_;
        float s = s_ * s_ * s_;
        float r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
        float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
        float bb = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
        bool inGamut = r >= 0.f && r <= 1.f && g >= 0.f && g <= 1.f && bb >= 0.f && bb <= 1.f;

        out = {clamp01(linearSrgb(r)), clamp01(linearSrgb(g)), clamp01(linearSrgb(bb)), 1.f};

        return inGamut;
    }

    // Preserve perceived lightness and hue, reducing only chroma until sRGB
    // can represent the result.  This is also the useful failure mode for
    // extreme picker colors near black and white.
    ThemeColor fromLch(Lch color) {
        ThemeColor out;

        color.l = clamp01(color.l);

        if (fromLchRaw(color, out)) {
            return out;
        }

        float lo = 0.f, hi = color.c;

        for (int i = 0; i < 16; i++) {
            color.c = (lo + hi) * 0.5f;

            if (fromLchRaw(color, out)) {
                lo = color.c;
            } else {
                hi = color.c;
            }
        }

        color.c = lo;
        fromLchRaw(color, out);

        return out;
    }

    void rgbToHsv(ThemeColor color, float& hue, float& saturation) {
        float maxv = color.r > color.g ? (color.r > color.b ? color.r : color.b) : (color.g > color.b ? color.g : color.b);
        float minv = color.r < color.g ? (color.r < color.b ? color.r : color.b) : (color.g < color.b ? color.g : color.b);
        float d = maxv - minv;

        saturation = maxv > 0.f ? d / maxv : 0.f;

        if (d <= 0.00001f) {
            // A colorless selection has no hue; retain the default blue role
            // so its procedural opposite remains orange.
            hue = 212.f / 360.f;
        } else if (maxv == color.r) {
            hue = fmodf((color.g - color.b) / d, 6.f) / 6.f;
        } else if (maxv == color.g) {
            hue = ((color.b - color.r) / d + 2.f) / 6.f;
        } else {
            hue = ((color.r - color.g) / d + 4.f) / 6.f;
        }

        if (hue < 0.f) {
            hue += 1.f;
        }
    }

    ThemeColor hsv(float hue, float saturation, float value) {
        hue -= floorf(hue);
        float h = hue * 6.f;
        int sector = (int)h;
        float f = h - sector;
        float p = value * (1.f - saturation);
        float q = value * (1.f - saturation * f);
        float t = value * (1.f - saturation * (1.f - f));

        switch (sector % 6) {
        case 0: return {value, t, p, 1.f};
        case 1: return {q, value, p, 1.f};
        case 2: return {p, value, t, 1.f};
        case 3: return {p, q, value, 1.f};
        case 4: return {t, p, value, 1.f};
        default: return {value, p, q, 1.f};
        }
    }

    void makeTones(ThemePalette& palette, Lch seed, float chromaCap) {
        seed.c = seed.c < chromaCap ? seed.c : chromaCap;

        for (int i = 0; i < ThemePalette::toneCount; i++) {
            float distance = i <= 5 ? (float)i / 5.f : (float)(10 - i) / 5.f;
            float l = i <= 5 ? seed.l * distance : seed.l + (1.f - seed.l) * (float)(i - 5) / 5.f;

            palette.tones[i] = fromLch({l, seed.c * distance, seed.h});
        }
    }

    ImVec4 im(ThemeColor color) {
        return {color.r, color.g, color.b, color.a};
    }
}

Theme::Theme() {
    rebuild();
}

void Theme::setSeeds(ThemeColor neutralColor, ThemeColor selectionColor) {
    neutralSeed = {clamp01(neutralColor.r), clamp01(neutralColor.g), clamp01(neutralColor.b), 1.f};
    selectionSeed = {clamp01(selectionColor.r), clamp01(selectionColor.g), clamp01(selectionColor.b), 1.f};
    rebuild();
}

void Theme::rebuild() {
    Lch n = toLch(neutralSeed);
    Lch s = toLch(selectionSeed);
    float artisticHue = 0.f, saturation = 0.f;

    rgbToHsv(selectionSeed, artisticHue, saturation);

    ThemeColor warm = hsv(artisticHue + 0.5f, saturation < 0.78f ? 0.78f : saturation, 0.96f);
    Lch a = toLch(warm);

    a.l = s.l + 0.10f;
    a.l = a.l < 0.62f ? 0.62f : a.l > 0.80f ? 0.80f : a.l;
    a.c = a.c < s.c * 0.95f ? s.c * 0.95f : a.c;
    a.c = a.c < 0.12f ? 0.12f : a.c > 0.20f ? 0.20f : a.c;
    accent = fromLch(a);
    a = toLch(accent);

    // A low-chroma cool neutral: selection and the direction opposite the
    // warm accent vote for its hue.  With blue/orange both votes agree.
    float x = 2.f * cosf(s.h) + cosf(a.h + 3.14159265358979323846f);
    float y = 2.f * sinf(s.h) + sinf(a.h + 3.14159265358979323846f);
    // The desktop is a neutral surface with only a trace of the interaction
    // palette.  Its huge area amplifies even small chroma, so keep the color
    // vote deliberately much weaker than on widgets.
    float desktopC = 0.055f * (s.c + a.c) * 0.5f;

    desktopC = desktopC < 0.006f ? 0.006f : desktopC > 0.018f ? 0.018f : desktopC;
    // Desktop is the deepest plane in the hierarchy.  Reduce perceptual
    // lightness after separating it from the neutral surface; doing this in
    // OKLCH avoids the hue-dependent result of simply halving sRGB channels.
    float desktopL = (n.l - 0.05f) * 0.8f;

    desktop = fromLch({desktopL, desktopC, atan2f(y, x)});

    makeTones(neutral, n, 0.025f);
    makeTones(selection, s, 0.20f);
    makeTones(accentTones, a, 0.20f);
    revision++;
}

ThemeColor themeAlpha(ThemeColor color, float alpha) {
    color.a = alpha;

    return color;
}

u32 themeColorU32(ThemeColor color) {
    u32 r = (u32)(clamp01(color.r) * 255.f + 0.5f);
    u32 g = (u32)(clamp01(color.g) * 255.f + 0.5f);
    u32 b = (u32)(clamp01(color.b) * 255.f + 0.5f);
    u32 a = (u32)(clamp01(color.a) * 255.f + 0.5f);

    return r | (g << 8) | (b << 16) | (a << 24);
}

void applyImGuiTheme(ImGuiStyle& style, const Theme& theme) {
    ImVec4* c = style.Colors;

    c[ImGuiCol_Text] = im(theme.neutral[10]);
    c[ImGuiCol_TextDisabled] = im(theme.neutral[8]);
    c[ImGuiCol_WindowBg] = im(theme.neutral[3]);
    c[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_PopupBg] = im(themeAlpha(theme.neutral[3], 0.96f));
    c[ImGuiCol_Border] = im(themeAlpha(theme.neutral[7], 0.50f));
    c[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_FrameBg] = im(themeAlpha(theme.neutral[5], 0.54f));
    c[ImGuiCol_FrameBgHovered] = im(themeAlpha(theme.selection[5], 0.40f));
    c[ImGuiCol_FrameBgActive] = im(themeAlpha(theme.selection[5], 0.67f));
    c[ImGuiCol_TitleBg] = im(theme.neutral[2]);
    c[ImGuiCol_TitleBgActive] = im(theme.selection[3]);
    c[ImGuiCol_TitleBgCollapsed] = im(themeAlpha(theme.neutral[1], 0.51f));
    c[ImGuiCol_MenuBarBg] = im(theme.neutral[5]);
    c[ImGuiCol_ScrollbarBg] = im(themeAlpha(theme.neutral[1], 0.53f));
    c[ImGuiCol_ScrollbarGrab] = im(theme.neutral[6]);
    c[ImGuiCol_ScrollbarGrabHovered] = im(theme.neutral[7]);
    c[ImGuiCol_ScrollbarGrabActive] = im(theme.neutral[8]);
    c[ImGuiCol_CheckMark] = im(theme.selection[5]);
    c[ImGuiCol_SliderGrab] = im(theme.selection[4]);
    c[ImGuiCol_SliderGrabActive] = im(theme.selection[5]);
    c[ImGuiCol_Button] = im(themeAlpha(theme.selection[5], 0.40f));
    c[ImGuiCol_ButtonHovered] = im(theme.selection[5]);
    c[ImGuiCol_ButtonActive] = im(theme.selection[4]);
    c[ImGuiCol_Header] = im(themeAlpha(theme.selection[5], 0.31f));
    c[ImGuiCol_HeaderHovered] = im(themeAlpha(theme.selection[5], 0.80f));
    c[ImGuiCol_HeaderActive] = im(theme.selection[5]);
    c[ImGuiCol_Separator] = im(themeAlpha(theme.neutral[7], 0.50f));
    c[ImGuiCol_SeparatorHovered] = im(themeAlpha(theme.selection[4], 0.78f));
    c[ImGuiCol_SeparatorActive] = im(theme.selection[4]);
    c[ImGuiCol_ResizeGrip] = im(themeAlpha(theme.selection[5], 0.20f));
    c[ImGuiCol_ResizeGripHovered] = im(themeAlpha(theme.selection[5], 0.67f));
    c[ImGuiCol_ResizeGripActive] = im(themeAlpha(theme.selection[5], 0.95f));
    c[ImGuiCol_InputTextCursor] = im(theme.selection[6]);
    c[ImGuiCol_TabHovered] = c[ImGuiCol_HeaderHovered];
    c[ImGuiCol_Tab] = im(theme.selection[2]);
    c[ImGuiCol_TabSelected] = im(theme.selection[3]);
    c[ImGuiCol_TabSelectedOverline] = im(theme.accent);
    c[ImGuiCol_TabDimmed] = im(theme.neutral[2]);
    c[ImGuiCol_TabDimmedSelected] = im(theme.neutral[3]);
    c[ImGuiCol_TabDimmedSelectedOverline] = im(themeAlpha(theme.accent, 0.f));
    c[ImGuiCol_DockingPreview] = im(themeAlpha(theme.selection[5], 0.70f));
    c[ImGuiCol_DockingEmptyBg] = im(theme.neutral[4]);
    c[ImGuiCol_PlotLines] = im(theme.neutral[8]);
    c[ImGuiCol_PlotLinesHovered] = im(theme.accent);
    c[ImGuiCol_PlotHistogram] = im(theme.accent);
    c[ImGuiCol_PlotHistogramHovered] = im(theme.accentTones[6]);
    c[ImGuiCol_TableHeaderBg] = im(theme.neutral[5]);
    c[ImGuiCol_TableBorderStrong] = im(theme.neutral[7]);
    c[ImGuiCol_TableBorderLight] = im(theme.neutral[6]);
    c[ImGuiCol_TableRowBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_TableRowBgAlt] = im(themeAlpha(theme.neutral[10], 0.06f));
    c[ImGuiCol_TextLink] = im(theme.selection[6]);
    c[ImGuiCol_TextSelectedBg] = im(themeAlpha(theme.selection[5], 0.35f));
    c[ImGuiCol_TreeLines] = im(themeAlpha(theme.neutral[8], 0.55f));
    c[ImGuiCol_DragDropTarget] = im(themeAlpha(theme.accent, 0.95f));
    c[ImGuiCol_UnsavedMarker] = im(theme.accent);
    c[ImGuiCol_NavCursor] = im(theme.selection[5]);
    c[ImGuiCol_NavWindowingHighlight] = im(themeAlpha(theme.neutral[10], 0.70f));
    c[ImGuiCol_NavWindowingDimBg] = im(themeAlpha(theme.neutral[2], 0.40f));
    c[ImGuiCol_ModalWindowDimBg] = im(themeAlpha(theme.neutral[2], 0.55f));
}
