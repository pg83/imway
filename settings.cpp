#include "settings.h"

#include "input.h"
#include "mixer.h"
#include "dialog.h"
#include "output.h"
#include "wayland.h"
#include "composer.h"
#include "imgui_wm.h"
#include "keyboard.h"

#include <std/dbg/assert.h>
#include <std/lib/buffer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

using namespace stl;

namespace {
    constexpr u32 anyModifiers = ~0u;

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

    int resizeText(ImGuiInputTextCallbackData* data) {
        auto& text = *(Buffer*)data->UserData;

        text.seekAbsolute((size_t)data->BufTextLen);
        text.grow((size_t)data->BufSize);
        data->Buf = text.cStr();

        return 0;
    }

    using BoolSetter = void (Settings::*)(bool);
    using FloatSetter = void (Settings::*)(float);
    using IntSetter = void (Settings::*)(int);
    using TextSetter = void (Settings::*)(StringView);

    void settingText(const char* id, Settings& settings, StringView value,
                     TextSetter setter) {
        Buffer text(value);
        char* data = text.cStr();

        if (ImGui::InputText(id, data, text.capacity(),
                             ImGuiInputTextFlags_CallbackResize, resizeText,
                             &text)) {
            (settings.*setter)(StringView((const char*)text.data()));
        }
    }

    void settingTextMultiline(const char* id, Settings& settings,
                              StringView value, float height,
                              TextSetter setter) {
        Buffer text(value);
        char* data = text.cStr();

        if (ImGui::InputTextMultiline(
                id, data, text.capacity(), ImVec2(-FLT_MIN, height),
                ImGuiInputTextFlags_CallbackResize, resizeText, &text)) {
            (settings.*setter)(StringView((const char*)text.data()));
        }
    }

    void settingCheckbox(const char* id, Settings& settings, bool value,
                         BoolSetter setter) {
        if (ImGui::Checkbox(id, &value)) {
            (settings.*setter)(value);
        }
    }

    void settingSliderFloat(const char* id, Settings& settings, float value,
                            float low, float high, const char* format,
                            FloatSetter setter) {
        if (ImGui::SliderFloat(id, &value, low, high, format,
                               ImGuiSliderFlags_AlwaysClamp)) {
            (settings.*setter)(value);
        }
    }

    void settingSliderInt(const char* id, Settings& settings, int value,
                          int low, int high, const char* format,
                          IntSetter setter) {
        if (ImGui::SliderInt(id, &value, low, high, format,
                             ImGuiSliderFlags_AlwaysClamp)) {
            (settings.*setter)(value);
        }
    }

    template <typename T, size_t N>
    void settingCombo(const char* id, Settings& settings, T value,
                      const char* const (&items)[N],
                      void (Settings::*setter)(T)) {
        int index = (int)value;

        if (ImGui::Combo(id, &index, items, (int)N)) {
            (settings.*setter)((T)index);
        }
    }

    void settingTime(const char* id, Settings& settings, int minute,
                     IntSetter setter) {
        char value[16];

        snprintf(value, sizeof(value), "%02d:%02d", minute / 60, minute % 60);
        ImGui::SetNextItemWidth(-72.f);

        if (ImGui::SliderInt(id, &minute, 0, 1439, "",
                             ImGuiSliderFlags_AlwaysClamp)) {
            (settings.*setter)(minute);
        }

        ImGui::SameLine();
        ImGui::TextUnformatted(value);
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
        if (!ImGui::BeginTable("rows", 2,
                               ImGuiTableFlags_SizingStretchProp)) {
            return false;
        }

        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch,
                                1.f);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch,
                                2.f);

        return true;
    }

    void drawAudioStatus(Composer& c) {
        if (c.mixer) {
            float volume = c.mixer->volume() * 100.f;
            bool muted = c.mixer->muted();

            row("muted");

            if (ImGui::Checkbox("##muted", &muted)) {
                c.mixer->setMuted(muted);
            }

            row("volume");

            if (ImGui::SliderFloat("##volume", &volume, 0.f, 100.f, "%.0f%%",
                                   ImGuiSliderFlags_AlwaysClamp)) {
                c.mixer->setVolume(volume / 100.f);
            }
        } else {
            row("mixer");
            ImGui::TextDisabled("unavailable");
        }
    }

    void drawActiveLayout(Composer& c) {
        if (!c.kb || !c.wayland) {
            return;
        }

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

    struct Dialog {
        int page = 0;
        float scaleEdit = 0.f;
        int* shortcutCapture = nullptr;

        void draw(Composer& c, Settings& settings, int& capture, bool& open);
        void drawGeneratedPage(Composer& c, Settings& settings);
        void pageDisplay(Composer& c, Settings& settings);
        void pageInput(Composer& c, Settings& settings);
        void pageShortcuts(Composer& c, Settings& settings);
        void pageNotifications(Composer& c, Settings& settings);
    };

#include "settings.dialog.gen.inc"
}

void applySettingsEnvironment(Settings& settings) {
    if (const char* terminal = getenv("IMWAY_TERMINAL");
        terminal && *terminal) {
        settings.setTerminal(StringView(terminal));
    } else if (const char* terminal = getenv("TERMINAL");
               terminal && *terminal) {
        settings.setTerminal(StringView(terminal));
    }
}

void Dialog::pageDisplay(Composer& c, Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("ui scale");

    if (scaleEdit <= 0.f) {
        scaleEdit = s.uiScale();
    }

    ImGui::SliderFloat("##scale", &scaleEdit, 1.f, 3.f, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp);

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        s.setUiScale(scaleEdit);
    }

    row("connector");
    settingText("##output", s, s.outputName(), &Settings::setOutputName);
    row("mode");
    settingText("##mode", s, s.outputMode(), &Settings::setOutputMode);

    if (c.output && c.output->hasBrightness()
        && !c.output->colorState().hdr()) {
        float brightness = c.output->brightness() * 100.f;

        row("brightness");

        if (ImGui::SliderFloat("##brightness", &brightness, 0.f, 100.f,
                               "%.0f%%",
                               ImGuiSliderFlags_AlwaysClamp)) {
            c.output->setBrightness(brightness / 100.f);
        }
    }

    row("brightness step");
    settingSliderFloat("##brightness-step", s, s.brightnessStep(), .01f,
                       .25f, "%.2f", &Settings::setBrightnessStep);
    row("hdr");
    settingCheckbox("enabled##hdr", s, s.hdrEnabled(),
                    &Settings::setHdrEnabled);

    if (s.hdrEnabled()) {
        row("sdr white");
        settingSliderFloat("##sdr-white", s, s.sdrNits(), 80.f, 300.f,
                           "%.0f nits", &Settings::setSdrNits);
        row("hdr key step");
        settingSliderFloat("##hdr-step", s, s.hdrStepNits(), 1.f, 50.f,
                           "%.0f nits", &Settings::setHdrStepNits);
        row("display minimum");
        settingSliderFloat("##display-min", s, s.displayMinNits(), 0.f, 10.f,
                           "%.3f nits", &Settings::setDisplayMinNits);
        row("display peak");
        settingSliderFloat("##display-peak", s, s.displayPeakNits(), 100.f,
                           10000.f, "%.0f nits",
                           &Settings::setDisplayPeakNits);
        row("display maxFALL");
        settingSliderFloat("##display-fall", s, s.displayMaxFallNits(), 100.f,
                           10000.f, "%.0f nits",
                           &Settings::setDisplayMaxFallNits);
    }

    row("bits per channel");

    {
        constexpr const char* items[] = {"auto", "8", "10", "12"};
        int value = s.outputBpc() == 8 ? 1
                  : s.outputBpc() == 10 ? 2
                  : s.outputBpc() == 12 ? 3 : 0;

        if (ImGui::Combo("##bpc", &value, items, 4)) {
            constexpr u32 values[] = {0, 8, 10, 12};

            s.setOutputBpc(values[value]);
        }
    }

    row("rgb range");

    {
        constexpr const char* items[] = {"auto", "full", "limited"};

        settingCombo("##range", s, s.outputRange(), items,
                     &Settings::setOutputRange);
    }

    row("display sleep");

    {
        float seconds = (float)s.dpmsSeconds();

        if (ImGui::SliderFloat("##dpms", &seconds, 0.f, 3600.f,
                               seconds > 0.f ? "%.0f sec" : "off",
                               ImGuiSliderFlags_AlwaysClamp)) {
            s.setDpmsSeconds(seconds);
        }
    }

    row("auto lock");

    {
        float seconds = (float)s.lockSeconds();

        if (ImGui::SliderFloat("##lock", &seconds, 0.f, 3600.f,
                               seconds > 0.f ? "%.0f sec" : "off",
                               ImGuiSliderFlags_AlwaysClamp)) {
            s.setLockSeconds(seconds);
        }
    }

    row("lock before sleep");
    settingCheckbox("##lock-before-dpms", s, s.lockBeforeDpms(),
                    &Settings::setLockBeforeDpms);
    row("osd duration");
    settingSliderFloat("##osd", s, s.osdSeconds(), .25f, 5.f, "%.2f sec",
                       &Settings::setOsdSeconds);
    row("osd fade");
    settingSliderFloat("##osd-fade", s, s.osdFadeSeconds(), .05f, 2.f,
                       "%.2f sec", &Settings::setOsdFadeSeconds);
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
    settingSliderFloat("##pointer-speed", s, s.pointerSpeed(), -1.f, 1.f,
                       "%.2f", &Settings::setPointerSpeed);
    row("acceleration");

    {
        constexpr const char* items[] = {"adaptive", "flat"};

        settingCombo("##accel-profile", s, s.pointerAccelProfile(), items,
                     &Settings::setPointerAccelProfile);
    }

    row("tap to click");
    settingCheckbox("##tap", s, s.tapToClick(), &Settings::setTapToClick);
    row("natural scroll");
    settingCheckbox("##natural", s, s.naturalScroll(),
                    &Settings::setNaturalScroll);
    row("left handed");
    settingCheckbox("##left-handed", s, s.leftHanded(),
                    &Settings::setLeftHanded);
    row("disable while typing");
    settingCheckbox("##dwt", s, s.disableWhileTyping(),
                    &Settings::setDisableWhileTyping);
    row("middle emulation");
    settingCheckbox("##middle", s, s.middleEmulation(),
                    &Settings::setMiddleEmulation);
    row("click method");

    {
        constexpr const char* items[] = {
            "auto", "button areas", "clickfinger",
        };

        settingCombo("##click-method", s, s.touchpadClickMethod(), items,
                     &Settings::setTouchpadClickMethod);
    }

    row("scroll method");

    {
        constexpr const char* items[] = {
            "auto", "two finger", "edge", "button",
        };

        settingCombo("##scroll-method", s, s.touchpadScrollMethod(), items,
                     &Settings::setTouchpadScrollMethod);
    }

    constexpr const char* gestureItems[] = {
        "none", "next window", "previous window",
        "launcher", "notifications", "lock",
    };

    row("swipe left");
    settingCombo("##swipe-left", s, s.swipeLeft(), gestureItems,
                 &Settings::setSwipeLeft);
    row("swipe right");
    settingCombo("##swipe-right", s, s.swipeRight(), gestureItems,
                 &Settings::setSwipeRight);
    row("swipe up");
    settingCombo("##swipe-up", s, s.swipeUp(), gestureItems,
                 &Settings::setSwipeUp);
    row("swipe down");
    settingCombo("##swipe-down", s, s.swipeDown(), gestureItems,
                 &Settings::setSwipeDown);
    row("pinch in");
    settingCombo("##pinch-in", s, s.pinchIn(), gestureItems,
                 &Settings::setPinchIn);
    row("pinch out");
    settingCombo("##pinch-out", s, s.pinchOut(), gestureItems,
                 &Settings::setPinchOut);
    ImGui::EndTable();

    size_t devices = s.inputDeviceCount();

    for (size_t i = 0;
         i < devices && i < Settings::inputDeviceCapacity; i++) {
        InputDeviceSettings value = s.inputDevice(i);

        ImGui::PushID((int)i);

        if (ImGui::TreeNode(value.name[0] ? value.name : "input device")) {
            ImGui::Checkbox("override", &value.enabled);

            if (value.enabled) {
                ImGui::Checkbox("pointer speed", &value.pointerSpeedSet);

                if (value.pointerSpeedSet) {
                    ImGui::SliderFloat("##device-speed", &value.pointerSpeed,
                                       -1.f, 1.f, "%.2f",
                                       ImGuiSliderFlags_AlwaysClamp);
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

            s.setInputDevice(i, value);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}

void Dialog::pageShortcuts(Composer&, Settings& s) {
    if (!ImGui::BeginTable(
            "shortcuts", 2,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn("binding", ImGuiTableColumnFlags_WidthStretch,
                            1.f);
    ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch,
                            2.f);

    for (size_t i = 0; i < Settings::shortcutCount; i++) {
        ShortcutBinding binding = s.shortcut(i);
        char chord[128];

        shortcutName(binding, chord);
        ImGui::PushID((int)i);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        const char* label = *shortcutCapture == (int)i
                          ? "press a key..." : chord;

        if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.f))) {
            *shortcutCapture = (int)i;
        }

        if (ImGui::BeginPopupContextItem("binding")) {
            if (ImGui::MenuItem("reset")) {
                s.setShortcut(i, generatedShortcutDefault(i));
            }

            ImGui::EndPopup();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(shortcutActionName(binding.action));
        ImGui::PopID();
    }

    ImGui::EndTable();
}

void Dialog::pageNotifications(Composer&, Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("do not disturb");
    settingCheckbox("##dnd", s, s.dnd(), &Settings::setDnd);
    row("dnd schedule");
    settingCheckbox("##dnd-schedule", s, s.dndScheduled(),
                    &Settings::setDndScheduled);

    if (s.dndScheduled()) {
        row("starts");
        settingTime("##dnd-start", s, s.dndStartMinute(),
                    &Settings::setDndStartMinute);
        row("ends");
        settingTime("##dnd-end", s, s.dndEndMinute(),
                    &Settings::setDndEndMinute);
    }

    row("default timeout");
    settingSliderFloat("##notification-timeout", s, s.notificationSeconds(),
                       1.f, 30.f, "%.1f sec",
                       &Settings::setNotificationSeconds);
    row("history limit");
    settingSliderInt("##notification-history", s, s.notificationHistory(), 0,
                     1000, "%d", &Settings::setNotificationHistory);
    row("toast width");
    settingSliderFloat("##notification-width", s, s.notificationWidth(),
                       200.f, 800.f, "%.0f px",
                       &Settings::setNotificationWidth);
    row("toast position");

    {
        constexpr const char* items[] = {
            "top right", "top left", "bottom right", "bottom left",
        };

        settingCombo("##toast-position", s, s.toastPosition(), items,
                     &Settings::setToastPosition);
    }

    row("wifi events");
    settingCheckbox("##notify-wifi", s, s.notifyWifi(),
                    &Settings::setNotifyWifi);
    row("critical notifications");
    settingCheckbox("##critical", s, s.allowCriticalNotifications(),
                    &Settings::setAllowCriticalNotifications);
    ImGui::EndTable();

    ImGui::SeparatorText("application rules");
    size_t count = s.notificationRuleCount();

    for (size_t i = 0;
         i < count && i < Settings::notificationRuleCapacity; i++) {
        NotificationRule rule = s.notificationRule(i);

        ImGui::PushID((int)i);
        ImGui::SetNextItemWidth(-130.f);

        if (ImGui::InputText("##app", rule.app, sizeof(rule.app))) {
            s.setNotificationRule(i, rule);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.f);

        {
            constexpr const char* items[] = {"default", "allow", "mute"};
            int policy = (int)rule.policy;

            if (ImGui::Combo("##policy", &policy, items, 3)) {
                rule.policy = (NotificationPolicy)policy;
                s.setNotificationRule(i, rule);
            }
        }

        ImGui::SameLine();

        if (ImGui::SmallButton("remove")) {
            for (size_t j = i + 1; j < count; j++) {
                s.setNotificationRule(j - 1, s.notificationRule(j));
            }

            s.setNotificationRuleCount(count - 1);
            count--;
            i--;
        }

        ImGui::PopID();
    }

    if (count < Settings::notificationRuleCapacity
        && ImGui::SmallButton("add application rule")) {
        s.setNotificationRule(count, {});
        s.setNotificationRuleCount(count + 1);
    }
}

void Dialog::draw(Composer& c, Settings& s, int& capture, bool& open) {
    float uiScale = s.uiScale();

    shortcutCapture = &capture;
    ImGui::SetNextWindowPos(ImVec2(80.f * uiScale, 80.f * uiScale),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760.f * uiScale, 520.f * uiScale),
                             ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("settings", &open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();

        return;
    }

    ImGui::BeginChild("nav", ImVec2(150.f * uiScale, 0.f));

    for (int i = 0;
         i < (int)(sizeof(generatedSettingsPages)
                   / sizeof(*generatedSettingsPages));
         i++) {
        if (ImGui::Selectable(generatedSettingsPages[i], page == i)) {
            page = i;
        }
    }

    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("page");
    drawGeneratedPage(c, s);
    ImGui::EndChild();
    ImGui::End();
}

void drawSettings(Composer& c, Settings& settings, bool toggle,
                  int& shortcutCapture, DialogState** state) {
    dialog<Dialog>(toggle, state, [&](Dialog& dialog, bool& open) {
        dialog.draw(c, settings, shortcutCapture, open);
    });
}
