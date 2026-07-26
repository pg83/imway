#include "settings.h"

#include "input.h"
#include "mixer.h"
#include "dialog.h"
#include "output.h"
#include "wayland.h"
#include "composer.h"
#include "keyboard.h"
#include "imgui_wm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

using namespace stl;

namespace {
    constexpr u32 anyModifiers = ~0u;

    ShortcutBinding defaultShortcut(size_t index) {
        constexpr ShortcutBinding defaults[] = {
            {ShortcutAction::screenshot, anyModifiers, XKB_KEY_Print},
            {ShortcutAction::lock, kModLogo, XKB_KEY_l},
            {ShortcutAction::launcher, kModLogo, XKB_KEY_F2},
            {ShortcutAction::inspector, kModLogo, XKB_KEY_F12},
            {ShortcutAction::altTabNext, kModAlt, XKB_KEY_Tab},
            {ShortcutAction::altTabPrev, kModAlt | kModShift, XKB_KEY_Tab},
        };

        return defaults[index];
    }

    const char* shortcutActionName(ShortcutAction action) {
        switch (action) {
            case ShortcutAction::screenshot:
                return "save a screenshot";
            case ShortcutAction::lock:
                return "lock the screen";
            case ShortcutAction::launcher:
                return "application launcher";
            case ShortcutAction::inspector:
                return "inspector";
            case ShortcutAction::altTabNext:
                return "next window";
            case ShortcutAction::altTabPrev:
                return "previous window";
        }

        return "action";
    }

    void shortcutName(const ShortcutBinding& binding, char out[128]) {
        char key[64] = {};

        if (xkb_keysym_get_name(binding.keysym, key, sizeof(key)) <= 0) {
            snprintf(key, sizeof(key), "0x%x", binding.keysym);
        }

        if (binding.modifiers == anyModifiers) {
            snprintf(out, 128, "%s", key);

            return;
        }

        size_t used = 0;

        auto append = [&](const char* value) {
            int n = snprintf(out + used, 128 - used, "%s", value);

            if (n > 0) {
                used += (size_t)n < 128 - used ? (size_t)n : 128 - used - 1;
            }
        };

        if (binding.modifiers & kModCtrl) {
            append("Ctrl+");
        }
        if (binding.modifiers & kModAlt) {
            append("Alt+");
        }
        if (binding.modifiers & kModShift) {
            append("Shift+");
        }
        if (binding.modifiers & kModLogo) {
            append("Super+");
        }

        append(key);
    }

    template <size_t N>
    void copyText(char (&out)[N], StringView value) {
        size_t n = value.length() < N - 1 ? value.length() : N - 1;

        memcpy(out, value.begin(), n);
        out[n] = 0;
    }

    template <size_t N>
    bool editText(const char* id, TextSetting<N>& setting) {
        char text[N];

        copyText(text, setting.get());

        return ImGui::InputText(id, text, N) && setting.set(StringView(text));
    }

    template <size_t N>
    bool editTextMultiline(const char* id, TextSetting<N>& setting, float height) {
        char text[N];

        copyText(text, setting.get());

        return ImGui::InputTextMultiline(id, text, N, ImVec2(-FLT_MIN, height)) && setting.set(StringView(text));
    }

    bool checkbox(const char* id, Setting<bool>& setting) {
        bool value = setting.get();

        return ImGui::Checkbox(id, &value) && setting.set(value);
    }

    bool sliderFloat(const char* id, Setting<float>& setting, float low, float high, const char* format) {
        float value = setting.get();

        return ImGui::SliderFloat(id, &value, low, high, format, ImGuiSliderFlags_AlwaysClamp) && setting.set(value);
    }

    bool sliderInt(const char* id, Setting<int>& setting, int low, int high, const char* format) {
        int value = setting.get();

        return ImGui::SliderInt(id, &value, low, high, format, ImGuiSliderFlags_AlwaysClamp) && setting.set(value);
    }

    template <typename T, size_t N>
    bool combo(const char* id, Setting<T>& setting, const char* const (&items)[N]) {
        int value = (int)setting.get();

        return ImGui::Combo(id, &value, items, (int)N) && setting.set((T)value);
    }

    void row(const char* label) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    bool beginRows() {
        if (!ImGui::BeginTable("rows", 2, ImGuiTableFlags_SizingStretchProp)) {
            return false;
        }

        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch, 2.f);

        return true;
    }

    void timeSetting(const char* id, Setting<int>& setting) {
        int minute = setting.get();
        char value[16];

        snprintf(value, sizeof(value), "%02d:%02d", minute / 60, minute % 60);
        ImGui::SetNextItemWidth(-72.f);

        if (ImGui::SliderInt(id, &minute, 0, 1439, "", ImGuiSliderFlags_AlwaysClamp)) {
            setting.set(minute);
        }

        ImGui::SameLine();
        ImGui::TextUnformatted(value);
    }

    struct Dialog {
        int page = 0;
        float scaleEdit = 0.f;

        void draw(Composer& c, Settings& settings, bool& open);
        void pageDisplay(Composer& c, Settings& settings);
        void pageColor(Settings& settings);
        void pageAppearance(Settings& settings);
        void pageAudio(Composer& c, Settings& settings);
        void pageInput(Composer& c, Settings& settings);
        void pageKeyboard(Composer& c, Settings& settings);
        void pageShortcuts(Settings& settings);
        void pageNotifications(Settings& settings);
        void pageDesktop(Settings& settings);
        void pageApplications(Settings& settings);
        void pageAdvanced(Settings& settings);
    };
}

void initializeSettings(Settings& settings) {
    if (const char* terminal = getenv("IMWAY_TERMINAL"); terminal && *terminal) {
        settings.terminal.set(StringView(terminal));
    } else if (const char* terminal = getenv("TERMINAL"); terminal && *terminal) {
        settings.terminal.set(StringView(terminal));
    }

    for (size_t i = 0; i < sizeof(settings.shortcuts) / sizeof(*settings.shortcuts); i++) {
        settings.shortcuts[i].set(defaultShortcut(i));
    }
}

void Dialog::pageDisplay(Composer& c, Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("ui scale");

    if (scaleEdit <= 0.f) {
        scaleEdit = s.uiScale.get();
    }

    ImGui::SliderFloat("##scale", &scaleEdit, 1.f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        s.uiScale.set(scaleEdit);
    }

    row("connector");
    editText("##output", s.outputName);
    row("mode");
    editText("##mode", s.outputMode);

    if (c.output && c.output->hasBrightness() && !c.output->colorState().hdr()) {
        float brightness = c.output->brightness() * 100.f;

        row("brightness");

        if (ImGui::SliderFloat("##brightness", &brightness, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            c.output->setBrightness(brightness / 100.f);
        }
    }

    row("brightness step");
    sliderFloat("##brightness-step", s.brightnessStep, .01f, .25f, "%.0f%%");
    row("hdr");
    checkbox("enabled##hdr", s.hdrEnabled);

    if (s.hdrEnabled.get()) {
        row("sdr white");
        sliderFloat("##sdr-white", s.sdrNits, 80.f, 300.f, "%.0f nits");
        row("hdr key step");
        sliderFloat("##hdr-step", s.hdrStepNits, 1.f, 50.f, "%.0f nits");
        row("display minimum");
        sliderFloat("##display-min", s.displayMinNits, 0.f, 10.f, "%.3f nits");
        row("display peak");
        sliderFloat("##display-peak", s.displayPeakNits, 100.f, 10000.f, "%.0f nits");
        row("display maxFALL");
        sliderFloat("##display-fall", s.displayMaxFallNits, 100.f, 10000.f, "%.0f nits");
    }

    row("bits per channel");

    {
        constexpr const char* items[] = {"auto", "8", "10", "12"};
        int value = s.outputBpc.get() == 8 ? 1 : s.outputBpc.get() == 10 ? 2 : s.outputBpc.get() == 12 ? 3 : 0;

        if (ImGui::Combo("##bpc", &value, items, 4)) {
            constexpr u32 values[] = {0, 8, 10, 12};

            s.outputBpc.set(values[value]);
        }
    }

    row("rgb range");

    {
        constexpr const char* items[] = {"auto", "full", "limited"};

        combo("##range", s.outputRange, items);
    }

    row("display sleep");

    {
        float seconds = (float)s.dpmsSeconds.get();

        if (ImGui::SliderFloat("##dpms", &seconds, 0.f, 3600.f, seconds > 0.f ? "%.0f sec" : "off", ImGuiSliderFlags_AlwaysClamp)) {
            s.dpmsSeconds.set(seconds);
        }
    }

    row("auto lock");

    {
        float seconds = (float)s.lockSeconds.get();

        if (ImGui::SliderFloat("##lock", &seconds, 0.f, 3600.f, seconds > 0.f ? "%.0f sec" : "off", ImGuiSliderFlags_AlwaysClamp)) {
            s.lockSeconds.set(seconds);
        }
    }

    row("lock before sleep");
    checkbox("##lock-before-dpms", s.lockBeforeDpms);
    row("osd duration");
    sliderFloat("##osd", s.osdSeconds, .25f, 5.f, "%.2f sec");
    row("osd fade");
    sliderFloat("##osd-fade", s.osdFadeSeconds, .05f, 2.f, "%.2f sec");
    ImGui::EndTable();
}

void Dialog::pageColor(Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("night light");
    checkbox("##night", s.nightOn);
    row("temperature");
    sliderFloat("##night-k", s.nightK, 2500.f, 6500.f, "%.0f K");
    row("schedule");
    checkbox("##night-schedule", s.nightScheduled);

    if (s.nightScheduled.get()) {
        row("starts");
        timeSetting("##night-start", s.nightStartMinute);
        row("ends");
        timeSetting("##night-end", s.nightEndMinute);
    }

    ImGui::EndTable();
}

void Dialog::pageAppearance(Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("variant");

    {
        constexpr const char* items[] = {"dark", "light", "system"};

        combo("##theme-variant", s.themeVariant, items);
    }

    row("neutral");

    {
        ThemeColor color = s.neutral.get();

        if (ImGui::ColorEdit3("##neutral", &color.r, ImGuiColorEditFlags_DisplayRGB)) {
            s.neutral.set(color);
        }
    }

    row("selection");

    {
        ThemeColor color = s.selection.get();

        if (ImGui::ColorEdit3("##selection", &color.r, ImGuiColorEditFlags_DisplayRGB)) {
            s.selection.set(color);
        }
    }

    row("font");
    editText("##font", s.fontPath);
    row("font size");
    sliderFloat("##font-size", s.fontSize, 8.f, 32.f, "%.0f px");
    row("icon theme");
    editText("##icon-theme", s.iconTheme);
    row("cursor scale");
    sliderFloat("##cursor-scale", s.cursorScale, .5f, 3.f, "%.2f");
    row("window shadows");
    checkbox("##shadows", s.windowShadows);

    if (s.windowShadows.get()) {
        row("shadow strength");
        sliderFloat("##shadow-strength", s.shadowStrength, 0.f, 2.f, "%.2f");
    }

    row("visual bell");
    checkbox("##visual-bell", s.visualBell);

    if (s.visualBell.get()) {
        row("bell duration");
        sliderFloat("##bell-seconds", s.visualBellSeconds, .02f, 1.f, "%.2f sec");
        row("bell strength");
        sliderFloat("##bell-strength", s.visualBellStrength, 0.f, 1.f, "%.2f");
    }

    row("lock blur");
    checkbox("##lock-blur", s.lockBlur);
    row("lock tint");
    sliderFloat("##lock-tint", s.lockTint, 0.f, 1.f, "%.2f");
    ImGui::EndTable();
}

void Dialog::pageAudio(Composer& c, Settings& s) {
    if (!beginRows()) {
        return;
    }

    if (c.mixer) {
        float volume = c.mixer->volume() * 100.f;
        bool muted = c.mixer->muted();

        row("muted");

        if (ImGui::Checkbox("##muted", &muted)) {
            c.mixer->setMuted(muted);
        }

        row("volume");

        if (ImGui::SliderFloat("##volume", &volume, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            c.mixer->setVolume(volume / 100.f);
        }
    } else {
        row("mixer");
        ImGui::TextDisabled("unavailable");
    }

    row("key step");
    sliderFloat("##volume-step", s.volumeStep, .01f, .25f, "%.0f%%");
    row("backend");

    {
        constexpr const char* items[] = {"auto", "sndio", "pulse", "disabled"};

        combo("##audio-backend", s.audioBackend, items);
    }

    ImGui::EndTable();
}

void Dialog::pageInput(Composer& c, Settings& s) {
    if (!beginRows()) {
        return;
    }

    if (!c.input) {
        row("libinput");
        ImGui::TextDisabled("unavailable");
    }

    row("pointer speed");
    sliderFloat("##pointer-speed", s.pointerSpeed, -1.f, 1.f, "%.2f");
    row("acceleration");

    {
        constexpr const char* items[] = {"adaptive", "flat"};

        combo("##accel-profile", s.pointerAccelProfile, items);
    }

    row("tap to click");
    checkbox("##tap", s.tapToClick);
    row("natural scroll");
    checkbox("##natural", s.naturalScroll);
    row("left handed");
    checkbox("##left-handed", s.leftHanded);
    row("disable while typing");
    checkbox("##dwt", s.disableWhileTyping);
    row("middle emulation");
    checkbox("##middle", s.middleEmulation);
    row("click method");

    {
        constexpr const char* items[] = {"auto", "button areas", "clickfinger"};

        combo("##click-method", s.touchpadClickMethod, items);
    }

    row("scroll method");

    {
        constexpr const char* items[] = {"auto", "two finger", "edge", "button"};

        combo("##scroll-method", s.touchpadScrollMethod, items);
    }

    constexpr const char* gestureItems[] = {"none", "next window", "previous window", "launcher", "notifications", "lock"};

    row("swipe left");
    combo("##swipe-left", s.swipeLeft, gestureItems);
    row("swipe right");
    combo("##swipe-right", s.swipeRight, gestureItems);
    row("swipe up");
    combo("##swipe-up", s.swipeUp, gestureItems);
    row("swipe down");
    combo("##swipe-down", s.swipeDown, gestureItems);
    row("pinch in");
    combo("##pinch-in", s.pinchIn, gestureItems);
    row("pinch out");
    combo("##pinch-out", s.pinchOut, gestureItems);
    ImGui::EndTable();

    size_t devices = s.inputDeviceCount.get();

    for (size_t i = 0; i < devices && i < sizeof(s.inputDevices) / sizeof(*s.inputDevices); i++) {
        InputDeviceSettings value = s.inputDevices[i].get();

        ImGui::PushID((int)i);

        if (ImGui::TreeNode(value.name[0] ? value.name : "input device")) {
            ImGui::Checkbox("override", &value.enabled);

            if (value.enabled) {
                ImGui::Checkbox("pointer speed", &value.pointerSpeedSet);

                if (value.pointerSpeedSet) {
                    ImGui::SliderFloat("##device-speed", &value.pointerSpeed, -1.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                }

                ImGui::Checkbox("natural scroll", &value.naturalScrollSet);

                if (value.naturalScrollSet) {
                    ImGui::SameLine();
                    ImGui::Checkbox("enabled##natural", &value.naturalScroll);
                }

                ImGui::Checkbox("left handed", &value.leftHandedSet);

                if (value.leftHandedSet) {
                    ImGui::SameLine();
                    ImGui::Checkbox("enabled##left", &value.leftHanded);
                }
            }

            s.inputDevices[i].set(value);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}

void Dialog::pageKeyboard(Composer& c, Settings& s) {
    if (!beginRows()) {
        return;
    }

    if (c.kb && c.wayland) {
        row("active layout");

        for (u32 i = 0; i < c.kb->layoutCount(); i++) {
            StringView layout = c.kb->layoutName(i);
            char name[80];

            copyText(name, layout);

            if (ImGui::RadioButton(name, c.kb->activeLayout() == i)) {
                c.wayland->setLayout(i);
            }
        }
    }

    row("layouts");
    editText("##layouts", s.xkbLayouts);
    row("xkb options");
    editText("##xkb-options", s.xkbOptions);
    row("layout scope");

    {
        constexpr const char* items[] = {"global", "per window"};

        combo("##layout-policy", s.layoutPolicy, items);
    }

    row("repeat rate");
    sliderInt("##repeat-rate", s.repeatRate, 1, 100, "%d Hz");
    row("repeat delay");
    sliderInt("##repeat-delay", s.repeatDelay, 100, 2000, "%d ms");

    ImGui::EndTable();
}

void Dialog::pageShortcuts(Settings& s) {
    if (!ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn("binding", ImGuiTableColumnFlags_WidthStretch, 1.f);
    ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 2.f);

    for (size_t i = 0; i < sizeof(s.shortcuts) / sizeof(*s.shortcuts); i++) {
        ShortcutBinding binding = s.shortcuts[i].get();
        char chord[128];

        shortcutName(binding, chord);
        ImGui::PushID((int)i);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        if (ImGui::Button(s.shortcutCapture == (int)i ? "press a key..." : chord, ImVec2(-FLT_MIN, 0.f))) {
            s.shortcutCapture = (int)i;
        }

        if (ImGui::BeginPopupContextItem("binding")) {
            if (ImGui::MenuItem("reset")) {
                s.shortcuts[i].set(defaultShortcut(i));
            }

            ImGui::EndPopup();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(shortcutActionName(binding.action));
        ImGui::PopID();
    }

    ImGui::EndTable();
}

void Dialog::pageNotifications(Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("do not disturb");
    checkbox("##dnd", s.dnd);
    row("dnd schedule");
    checkbox("##dnd-schedule", s.dndScheduled);

    if (s.dndScheduled.get()) {
        row("starts");
        timeSetting("##dnd-start", s.dndStartMinute);
        row("ends");
        timeSetting("##dnd-end", s.dndEndMinute);
    }

    row("default timeout");
    sliderFloat("##notification-timeout", s.notificationSeconds, 1.f, 30.f, "%.1f sec");
    row("history limit");
    sliderInt("##notification-history", s.notificationHistory, 0, 1000, "%d");
    row("toast width");
    sliderFloat("##notification-width", s.notificationWidth, 200.f, 800.f, "%.0f px");
    row("toast position");

    {
        constexpr const char* items[] = {"top right", "top left", "bottom right", "bottom left"};

        combo("##toast-position", s.toastPosition, items);
    }

    row("wifi events");
    checkbox("##notify-wifi", s.notifyWifi);
    row("critical notifications");
    checkbox("##critical", s.allowCriticalNotifications);
    ImGui::EndTable();

    ImGui::SeparatorText("application rules");
    size_t count = s.notificationRuleCount.get();

    for (size_t i = 0; i < count && i < sizeof(s.notificationRules) / sizeof(*s.notificationRules); i++) {
        NotificationRule rule = s.notificationRules[i].get();

        ImGui::PushID((int)i);
        ImGui::SetNextItemWidth(-130.f);

        if (ImGui::InputText("##app", rule.app, sizeof(rule.app))) {
            s.notificationRules[i].set(rule);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.f);

        {
            constexpr const char* items[] = {"default", "allow", "mute"};
            int policy = (int)rule.policy;

            if (ImGui::Combo("##policy", &policy, items, 3)) {
                rule.policy = (NotificationPolicy)policy;
                s.notificationRules[i].set(rule);
            }
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("remove")) {
            for (size_t j = i + 1; j < count; j++) {
                s.notificationRules[j - 1].set(s.notificationRules[j].get());
            }

            s.notificationRuleCount.set(count - 1);
            count--;
            i--;
        }

        ImGui::PopID();
    }

    if (count < sizeof(s.notificationRules) / sizeof(*s.notificationRules) && ImGui::SmallButton("add application rule")) {
        s.notificationRules[count].set({});
        s.notificationRuleCount.set(count + 1);
    }
}

void Dialog::pageDesktop(Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("ItemIsMenu primary");
    checkbox("open DBusMenu", s.trayMenuOnPrimary);
    row("dock");
    checkbox("visible##dock", s.dockVisible);
    row("dock position");

    {
        constexpr const char* items[] = {"left", "right", "top", "bottom"};

        combo("##dock-position", s.dockPosition, items);
    }

    row("auto hide");
    checkbox("##dock-autohide", s.dockAutoHide);
    row("dock width");
    sliderFloat("##dock-width", s.dockWidth, 32.f, 128.f, "%.0f px");
    row("icon size");
    sliderFloat("##dock-icons", s.dockIconSize, 16.f, 96.f, "%.0f px");
    row("group windows");
    checkbox("##dock-group", s.dockGroupWindows);
    row("show tray");
    checkbox("##dock-tray", s.dockShowTray);
    row("merge tray");
    checkbox("##dock-merge", s.dockMergeTray);
    row("mru order");
    checkbox("##dock-mru", s.dockMruOrder);
    row("active click");

    {
        constexpr const char* items[] = {"focus", "minimize", "cycle"};

        combo("##dock-click", s.dockClickAction, items);
    }

    row("pinned apps");
    editText("##dock-pinned", s.dockPinned);
    row("top bar");
    checkbox("visible##topbar", s.topBarVisible);
    row("focused app id");
    checkbox("##topbar-app", s.topBarAppId);
    row("global menu");
    checkbox("##global-menu", s.topBarGlobalMenu);
    row("layout indicator");
    checkbox("##topbar-layout", s.topBarLayout);
    row("wifi indicator");
    checkbox("##topbar-wifi", s.topBarWifi);
    row("battery");

    {
        constexpr const char* items[] = {"never", "when discharging", "always"};

        combo("##battery", s.topBarBattery, items);
    }

    row("24 hour clock");
    checkbox("##clock-24", s.clock24Hour);
    row("clock date");
    checkbox("##clock-date", s.clockShowDate);
    row("clock seconds");
    checkbox("##clock-seconds", s.clockShowSeconds);
    row("locale formats");
    checkbox("##clock-locale", s.clockLocale);
    row("window docking");
    checkbox("##window-docking", s.imguiDocking);
    row("focus");

    {
        constexpr const char* items[] = {"click", "follows pointer"};

        combo("##focus", s.focusPolicy, items);
    }

    row("raise on focus");
    checkbox("##raise", s.raiseOnFocus);
    row("decorations");

    {
        constexpr const char* items[] = {"server", "client", "client preference"};

        combo("##decorations", s.decorations, items);
    }

    row("remember layout");
    checkbox("##remember-layout", s.rememberWindowLayout);
    ImGui::EndTable();
}

void Dialog::pageApplications(Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("terminal");
    editText("##terminal", s.terminal);
    row("terminal exec");
    editText("##terminal-exec", s.terminalExec);
    row("launcher shell");
    checkbox("##launcher-shell", s.launcherShellCommands);
    row("screenshot action");

    {
        constexpr const char* items[] = {"editor", "save", "copy"};

        combo("##screenshot-action", s.screenshotAction, items);
    }

    row("screenshot directory");
    editText("##screenshot-dir", s.screenshotDirectory);
    row("filename template");
    editText("##screenshot-name", s.screenshotName);
    row("format");

    {
        constexpr const char* items[] = {"jxl", "png"};

        combo("##screenshot-format", s.screenshotFormat, items);
    }

    row("lossless");
    checkbox("##screenshot-lossless", s.screenshotLossless);

    if (!s.screenshotLossless.get()) {
        row("quality");
        sliderFloat("##screenshot-quality", s.screenshotQuality, 1.f, 100.f, "%.0f%%");
    }

    row("wifi backend");

    {
        constexpr const char* items[] = {"auto", "iwd", "NetworkManager", "disabled"};

        combo("##wifi-backend", s.wifiBackend, items);
    }

    ImGui::EndTable();
    ImGui::SeparatorText("autostart commands");
    editTextMultiline("##autostart", s.autostart, ImGui::GetTextLineHeightWithSpacing() * 6.f);
}

void Dialog::pageAdvanced(Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("direct scanout");
    checkbox("##direct-scanout", s.directScanout);
    row("tearing");

    {
        constexpr const char* items[] = {"deny", "client requested", "always"};

        combo("##tearing", s.tearing, items);
    }

    row("hardware cursor");
    checkbox("##hardware-cursor", s.hardwareCursor);
    row("dithering");
    checkbox("##dithering", s.dithering);
    row("unresponsive after");
    sliderFloat("##anr", s.anrSeconds, 1.f, 60.f, "%.1f sec");
    row("seat backend");

    {
        constexpr const char* items[] = {"auto", "libseat", "direct"};

        combo("##seat", s.seatBackend, items);
    }

    row("pam service");
    editText("##pam", s.pamService);
    ImGui::EndTable();
}

void Dialog::draw(Composer& c, Settings& s, bool& open) {
    float uiScale = s.uiScale.get();
    constexpr const char* pages[] = {
        "display",
        "color",
        "appearance",
        "audio",
        "input",
        "keyboard",
        "shortcuts",
        "notifications",
        "desktop",
        "applications",
        "advanced",
    };

    ImGui::SetNextWindowPos(ImVec2(80.f * uiScale, 80.f * uiScale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760.f * uiScale, 520.f * uiScale), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("settings", &open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();

        return;
    }

    ImGui::BeginChild("nav", ImVec2(150.f * uiScale, 0.f));

    for (int i = 0; i < (int)(sizeof(pages) / sizeof(*pages)); i++) {
        if (ImGui::Selectable(pages[i], page == i)) {
            page = i;
        }
    }

    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("page");

    switch (page) {
        case 0:
            pageDisplay(c, s);
            break;
        case 1:
            pageColor(s);
            break;
        case 2:
            pageAppearance(s);
            break;
        case 3:
            pageAudio(c, s);
            break;
        case 4:
            pageInput(c, s);
            break;
        case 5:
            pageKeyboard(c, s);
            break;
        case 6:
            pageShortcuts(s);
            break;
        case 7:
            pageNotifications(s);
            break;
        case 8:
            pageDesktop(s);
            break;
        case 9:
            pageApplications(s);
            break;
        case 10:
            pageAdvanced(s);
            break;
    }

    ImGui::EndChild();
    ImGui::End();
}

void drawSettings(Composer& c, Settings& settings, bool toggle, DialogState** state) {
    dialog<Dialog>(toggle, state, [&](Dialog& dialog, bool& open) {
        dialog.draw(c, settings, open);
    });
}
