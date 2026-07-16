#pragma once

// the settings menu state: the caller owns one of these, the widget edits
// the values in place and raises the *Changed flags for whatever moved this
// frame — the caller applies the side effects (rescale, kms sdr white,
// gamma ramp) and the flags reset on the next draw
struct Settings {
    float uiScale = 1.f;   // in: the current scale, sizes the menu items
    float scaleEdit = 0.f; // slider-side scale value, 0 = seed from uiScale
    float scale = 0.f;     // committed scale, lands on slider release only —
                           // applying mid-drag would rescale the slider
                           // under the cursor
    float sdrNits = -1.f;  // sdr white; <= 0 renders the hdr row disabled
    bool nightOn = false;  // night light toggle + temperature
    float nightK = 3400.f;

    bool scaleChanged = false;
    bool sdrChanged = false;
    bool nightChanged = false;

    bool changed() const {
        return scaleChanged || sdrChanged || nightChanged;
    }
};

// imgui calls inside an open main menu bar
void drawSettingsMenu(Settings& s);
