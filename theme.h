#pragma once

#include <std/sys/types.h>

struct ImGuiStyle;

struct ThemeColor {
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
    float a = 1.f;
};

struct ThemePalette {
    static constexpr int toneCount = 11;
    ThemeColor tones[toneCount];

    const ThemeColor& operator[](int i) const {
        return tones[i];
    }
};

// Two user-owned key colors produce the entire desktop theme.  neutral is
// the structural surface key, selection is the interaction key; accent,
// desktop and the three tonal ramps are derived together on every edit.
struct Theme {
    ThemeColor neutralSeed{0.14f, 0.14f, 0.14f, 1.f};
    ThemeColor selectionSeed{0.26f, 0.59f, 0.98f, 1.f};

    ThemeColor accent;
    ThemeColor desktop;
    ThemePalette neutral;
    ThemePalette selection;
    ThemePalette accentTones;
    u64 revision = 0;

    Theme();
    void setSeeds(ThemeColor neutralColor, ThemeColor selectionColor);
    void rebuild();
};

// ImGui's style is a table of concrete roles, not a semantic palette.  This
// maps Theme's neutral/selection/accent roles onto that table without changing
// sizes, fonts or any other ImGui state.
void applyImGuiTheme(ImGuiStyle& style, const Theme& theme);

// ImGui/Vulkan's packed UI color convention: R in the low byte, then G/B/A.
u32 themeColorU32(ThemeColor color);
ThemeColor themeAlpha(ThemeColor color, float alpha);
