#pragma once

#include "color.h"
#include "theme.h"

#include <std/str/view.h>
#include <std/sys/types.h>

enum class ThemeVariant {
    dark,
    light,
    system,
};

enum class DockPosition {
    left,
    right,
    top,
    bottom,
};

enum class DockClickAction {
    focus,
    minimize,
    cycle,
};

enum class BatteryDisplay {
    never,
    discharging,
    always,
};

enum class FocusPolicy {
    click,
    followsPointer,
};

enum class DecorationPolicy {
    server,
    client,
    clientPreference,
};

enum class LayoutPolicy {
    global,
    perWindow,
};

enum class PointerAccelProfile {
    adaptive,
    flat,
};

enum class TouchpadClickMethod {
    automatic,
    buttonAreas,
    clickfinger,
};

enum class TouchpadScrollMethod {
    automatic,
    twoFinger,
    edge,
    button,
};

enum class ToastPosition {
    topRight,
    topLeft,
    bottomRight,
    bottomLeft,
};

enum class ScreenshotFormat {
    jxl,
    png,
};

enum class ScreenshotAction {
    editor,
    save,
    copy,
};

enum class BackendPreference {
    automatic,
    first,
    second,
    disabled,
};

enum class SeatBackend {
    automatic,
    libseat,
    direct,
};

enum class TearingPolicy {
    deny,
    client,
    always,
};

enum class GestureAction {
    none,
    altTabNext,
    altTabPrev,
    launcher,
    notifications,
    lock,
};

enum class NotificationPolicy {
    defaultPolicy,
    allow,
    mute,
};

enum class ShortcutAction {
    screenshot,
    lock,
    launcher,
    inspector,
    altTabNext,
    altTabPrev,
};

struct ShortcutBinding {
    ShortcutAction action = ShortcutAction::screenshot;
    u32 modifiers = 0;
    u32 keysym = 0;

    bool operator==(const ShortcutBinding&) const = default;
};

struct NotificationRule {
    char app[128] = "";
    NotificationPolicy policy = NotificationPolicy::defaultPolicy;

    bool operator==(const NotificationRule&) const = default;
};

struct InputDeviceSettings {
    char id[64] = "";
    char name[128] = "";
    bool enabled = false;
    bool pointerSpeedSet = false;
    float pointerSpeed = 0.f;
    bool naturalScrollSet = false;
    bool naturalScroll = false;
    bool leftHandedSet = false;
    bool leftHanded = false;

    bool operator==(const InputDeviceSettings&) const = default;
};

struct Composer;
struct DialogState;
struct Listener;

struct Settings {
    virtual ~Settings() = default;

#include "settings.gen.h"

    static Settings* create(Composer& composer);
};

void applySettingsEnvironment(Settings& settings);

// Plain pool-owned ImGui dialog. nullptr state means closed; toggle flips it.
void drawSettings(Composer& c, Settings& settings, bool toggle,
                  int& shortcutCapture, DialogState** state);
