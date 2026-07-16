#pragma once

// the settings menu state: the caller owns one of these, the widget edits
// the values in place and raises the *Changed flags for whatever moved this
// frame — the caller applies the side effects (rescale, kms sdr white,
// gamma ramp) and the flags reset on the next draw
struct Settings {
    float uiScale = 1.f;   // in: the current scale, sizes the menu items
    float volume = -1.f;   // 0..1; < 0 = no mixer, the row is hidden
    bool volMuted = false;
    float brightness = -1.f; // 0..1 panel backlight; < 0 = none, row hidden
    float scaleEdit = 0.f; // slider-side scale value, 0 = seed from uiScale
    float scale = 0.f;     // committed scale, lands on slider release only —
                           // applying mid-drag would rescale the slider
                           // under the cursor
    float sdrNits = -1.f;  // sdr white; <= 0 renders the hdr row disabled
    bool nightOn = false;  // night light toggle + temperature
    float nightK = 3400.f;

    bool hasDnd = false;   // in: a notifier exists, else the row is hidden
    bool dnd = false;      // do-not-disturb, edited in place

    bool open = false;     // menu visible this frame — the osd keeps quiet

    bool volumeChanged = false;
    bool muteChanged = false;
    bool brightnessChanged = false;
    bool scaleChanged = false;
    bool sdrChanged = false;
    bool nightChanged = false;
    bool dndChanged = false;

    bool changed() const {
        return volumeChanged || muteChanged || brightnessChanged || scaleChanged || sdrChanged || nightChanged || dndChanged;
    }
};

// imgui calls inside an open main menu bar
void drawSettingsMenu(Settings& s);
