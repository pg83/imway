#pragma once

#include "theme.h"

#include <std/str/view.h>
#include <std/sys/types.h>

// one row of the read-only keybindings page; the renderer owns the list —
// the same table drives its chord dispatch, so the view can never drift
struct KeyBinding {
    stl::StringView chord;
    stl::StringView action;
};

// the settings dialog model: the caller owns one of these, the widget edits
// the values in place and raises the *Changed flags for whatever moved this
// frame — the caller applies the side effects (rescale, kms sdr white,
// output color transform) and the flags reset on the next draw
struct Settings {
    float uiScale = 1.f; // in: the current scale, sizes the menu items
    float volume = -1.f; // 0..1; < 0 = no mixer, the row is hidden
    bool volMuted = false;
    float brightness = -1.f; // 0..1 panel backlight; < 0 = none, row hidden
    float scaleEdit = 0.f;   // slider-side scale value, 0 = seed from uiScale
    float scale = 0.f;       // committed scale, lands on slider release only —
                             // applying mid-drag would rescale the slider
                             // under the cursor
    float sdrNits = -1.f;    // sdr white; <= 0 renders the hdr row disabled
    float hdrPeakNits = 0.f;
    bool nightOn = false; // night light toggle + temperature
    float nightK = 3400.f;
    ThemeColor neutral;
    ThemeColor selection;

    bool hasDnd = false; // in: a notifier exists, else the row is hidden
    bool dnd = false;    // do-not-disturb, edited in place

    // input page: layout groups of the one compiled keymap (names point into
    // it, valid for the frame), plus the libinput accel bias
    static constexpr u32 kMaxLayouts = 8;
    stl::StringView layouts[kMaxLayouts];
    u32 layoutCount = 0; // in: 0 = no keyboard, the row is hidden
    u32 layoutActive = 0;
    u32 layoutSel = 0; // out: the group the user clicked
    stl::StringView xkbOptions;
    bool hasPointer = false;  // in: libinput exists, else the row is hidden
    float pointerSpeed = 0.f; // -1..1 accel bias, edited in place

    // keybindings page, view only
    const KeyBinding* bindings = nullptr;
    size_t bindingCount = 0;

    bool volumeChanged = false;
    bool muteChanged = false;
    bool brightnessChanged = false;
    bool scaleChanged = false;
    bool sdrChanged = false;
    bool nightChanged = false;
    bool dndChanged = false;
    bool themeChanged = false;
    bool layoutChanged = false;
    bool pointerChanged = false;

    bool changed() const {
        return volumeChanged || muteChanged || brightnessChanged || scaleChanged || sdrChanged || nightChanged || dndChanged || themeChanged || layoutChanged || pointerChanged;
    }
};

struct Composer;
struct DialogState;

// Plain pool-owned ImGui dialog. nullptr state means closed; toggle flips it.
void drawSettings(Composer& c, Settings& s, bool toggle, DialogState** state);
