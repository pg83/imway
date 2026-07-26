#pragma once

#include "color.h"
#include "theme.h"

#include <std/lib/list.h>
#include <std/str/view.h>
#include <std/sys/types.h>

// A setting commits its canonical value before publishing. Each setting owns
// a distinct removal-safe listener list; there is no aggregate dirty state.
void publishSettingListeners(stl::IntrusiveList& listeners);

template <typename T>
struct Setting {
    Setting() = default;

    Setting(const T& initial)
        : value_(initial)
    {
    }

    const T& get() const {
        return value_;
    }

    bool set(const T& value) {
        if (value_ == value) {
            return false;
        }

        value_ = value;
        publishSettingListeners(changedListeners);

        return true;
    }

    stl::IntrusiveList changedListeners;

private:
    T value_{};
};

template <size_t Capacity>
struct TextSetting {
    static_assert(Capacity > 0);

    TextSetting() = default;

    TextSetting(const char* initial) {
        setInitial(stl::StringView(initial));
    }

    stl::StringView get() const {
        return stl::StringView(value_);
    }

    bool set(stl::StringView value) {
        size_t length = value.length() < Capacity - 1 ? value.length() : Capacity - 1;

        if (get() == value.prefix(length)) {
            return false;
        }

        setInitial(value);
        publishSettingListeners(changedListeners);

        return true;
    }

    stl::IntrusiveList changedListeners;

private:
    void setInitial(stl::StringView value) {
        size_t n = value.length() < Capacity - 1 ? value.length() : Capacity - 1;

        for (size_t i = 0; i < n; i++) {
            value_[i] = (char)value[i];
        }

        value_[n] = 0;
    }

    char value_[Capacity] = {};
};

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

// The authoritative runtime preference set. Composer owns one instance.
// Persistence is intentionally outside this type.
struct Settings {
    // display and power
    Setting<float> uiScale{1.f};
    Setting<float> brightnessStep{.05f};
    Setting<float> hdrStepNits{10.f};
    Setting<float> osdSeconds{1.5f};
    Setting<float> osdFadeSeconds{.3f};
    Setting<double> dpmsSeconds{0.};
    Setting<double> lockSeconds{0.};
    Setting<bool> lockBeforeDpms{true};
    Setting<bool> hdrEnabled{false};
    Setting<float> sdrNits{80.f};
    Setting<float> displayMinNits{0.f};
    Setting<float> displayPeakNits{0.f};
    Setting<float> displayMaxFallNits{0.f};
    Setting<u32> outputBpc{0};
    Setting<OutputRange> outputRange{OutputRange::automatic};
    TextSetting<64> outputName;
    TextSetting<64> outputMode;

    // color and appearance
    Setting<bool> nightOn{false};
    Setting<bool> nightScheduled{false};
    Setting<int> nightStartMinute{20 * 60};
    Setting<int> nightEndMinute{7 * 60};
    Setting<float> nightK{3400.f};
    Setting<ThemeVariant> themeVariant{ThemeVariant::dark};
    Setting<ThemeColor> neutral{ThemeColor{.14f, .14f, .14f, 1.f}};
    Setting<ThemeColor> selection{ThemeColor{.26f, .59f, .98f, 1.f}};
    Setting<float> fontSize{16.f};
    Setting<float> cursorScale{1.f};
    Setting<bool> windowShadows{true};
    Setting<float> shadowStrength{1.f};
    Setting<bool> visualBell{true};
    Setting<float> visualBellSeconds{.15f};
    Setting<float> visualBellStrength{.35f};
    Setting<bool> lockBlur{true};
    Setting<float> lockTint{.42f};
    TextSetting<256> fontPath;
    TextSetting<64> iconTheme{"hicolor"};

    // audio
    Setting<float> volumeStep{.05f};
    Setting<BackendPreference> audioBackend{BackendPreference::automatic};

    // keyboard and input
    TextSetting<128> xkbLayouts{"us,ru"};
    TextSetting<128> xkbOptions{"grp:caps_toggle"};
    Setting<int> repeatRate{25};
    Setting<int> repeatDelay{600};
    Setting<LayoutPolicy> layoutPolicy{LayoutPolicy::perWindow};
    Setting<float> pointerSpeed{0.f};
    Setting<PointerAccelProfile> pointerAccelProfile{PointerAccelProfile::adaptive};
    Setting<bool> tapToClick{true};
    Setting<bool> naturalScroll{false};
    Setting<bool> leftHanded{false};
    Setting<bool> disableWhileTyping{true};
    Setting<bool> middleEmulation{false};
    Setting<TouchpadClickMethod> touchpadClickMethod{TouchpadClickMethod::automatic};
    Setting<TouchpadScrollMethod> touchpadScrollMethod{TouchpadScrollMethod::automatic};
    Setting<GestureAction> swipeLeft{GestureAction::none};
    Setting<GestureAction> swipeRight{GestureAction::none};
    Setting<GestureAction> swipeUp{GestureAction::none};
    Setting<GestureAction> swipeDown{GestureAction::none};
    Setting<GestureAction> pinchIn{GestureAction::none};
    Setting<GestureAction> pinchOut{GestureAction::none};
    Setting<InputDeviceSettings> inputDevices[16];
    Setting<size_t> inputDeviceCount{0};
    Setting<ShortcutBinding> shortcuts[6];
    int shortcutCapture = -1; // transient dialog state, not a preference

    // desktop and window management
    Setting<bool> dockVisible{true};
    Setting<bool> dockAutoHide{false};
    Setting<DockPosition> dockPosition{DockPosition::left};
    Setting<float> dockWidth{58.f};
    Setting<float> dockIconSize{48.f};
    Setting<bool> dockGroupWindows{true};
    Setting<bool> dockShowTray{true};
    Setting<bool> dockMergeTray{true};
    Setting<bool> dockMruOrder{true};
    TextSetting<1024> dockPinned;
    Setting<DockClickAction> dockClickAction{DockClickAction::focus};
    Setting<bool> topBarVisible{true};
    Setting<bool> topBarAppId{true};
    Setting<bool> topBarGlobalMenu{true};
    Setting<bool> topBarLayout{true};
    Setting<bool> topBarWifi{true};
    Setting<BatteryDisplay> topBarBattery{BatteryDisplay::discharging};
    Setting<bool> clock24Hour{true};
    Setting<bool> clockShowDate{true};
    Setting<bool> clockShowSeconds{false};
    Setting<bool> clockLocale{true};
    Setting<bool> imguiDocking{true};
    Setting<FocusPolicy> focusPolicy{FocusPolicy::click};
    Setting<bool> raiseOnFocus{true};
    Setting<DecorationPolicy> decorations{DecorationPolicy::server};
    Setting<bool> rememberWindowLayout{true};
    Setting<bool> trayMenuOnPrimary{true};

    // notifications
    Setting<bool> dnd{false};
    Setting<bool> dndScheduled{false};
    Setting<int> dndStartMinute{22 * 60};
    Setting<int> dndEndMinute{7 * 60};
    Setting<float> notificationSeconds{5.f};
    Setting<int> notificationHistory{50};
    Setting<float> notificationWidth{320.f};
    Setting<ToastPosition> toastPosition{ToastPosition::topRight};
    Setting<bool> notifyWifi{true};
    Setting<bool> allowCriticalNotifications{true};
    Setting<NotificationRule> notificationRules[32];
    Setting<size_t> notificationRuleCount{0};

    // applications and actions
    TextSetting<128> terminal{"zutty"};
    TextSetting<128> terminalExec{"-e sh -c"};
    TextSetting<1024> autostart;
    Setting<bool> launcherShellCommands{true};
    TextSetting<256> screenshotDirectory;
    TextSetting<128> screenshotName{"imway-%Y%m%d-%H%M%S"};
    Setting<ScreenshotFormat> screenshotFormat{ScreenshotFormat::jxl};
    Setting<ScreenshotAction> screenshotAction{ScreenshotAction::editor};
    Setting<bool> screenshotLossless{true};
    Setting<float> screenshotQuality{90.f};
    Setting<BackendPreference> wifiBackend{BackendPreference::automatic};

    // advanced and deployment policy
    Setting<bool> directScanout{true};
    Setting<TearingPolicy> tearing{TearingPolicy::client};
    Setting<bool> hardwareCursor{true};
    Setting<bool> dithering{true};
    Setting<float> anrSeconds{10.f};
    Setting<SeatBackend> seatBackend{SeatBackend::automatic};
    TextSetting<64> pamService{"login"};
};

struct Composer;
struct DialogState;

void initializeSettings(Settings& settings);

// Plain pool-owned ImGui dialog. nullptr state means closed; toggle flips it.
void drawSettings(Composer& c, Settings& settings, bool toggle, DialogState** state);
