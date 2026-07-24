#include "settings.h"

#include "composer.h"
#include "dialog.h"

#include <imgui.h>
#include <math.h>
#include <string.h>

using namespace stl;

namespace {
    // two-pane layout: page names on the left, the active page's rows on
    // the right; the transient page index dies with the dialog
    constexpr const char* kPages[] = {
        "display",
        "color",
        "audio",
        "input",
        "keys",
        "notifications",
    };

    struct Dialog {
        int page = 0;

        void draw(Settings& s, bool& open);
        void pageDisplay(Settings& s);
        void pageColor(Settings& s);
        void pageAudio(Settings& s);
        void pageInput(Settings& s);
        void pageKeys(Settings& s);
        void pageNotifications(Settings& s);
    };

    // label | control rows; every page lays out through these so the
    // controls align, instead of per-widget hand-set widths
    bool beginRows() {
        if (!ImGui::BeginTable("rows", 2, ImGuiTableFlags_SizingStretchProp)) {
            return false;
        }

        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch, 2.f);

        return true;
    }

    void row(const char* label) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }
}

void Dialog::pageDisplay(Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("scale");
    ImGui::SliderFloat("##scale", &s.scaleEdit, 1.f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        s.scale = s.scaleEdit;
        s.scaleChanged = true;
    }

    if (s.brightness >= 0.f) {
        float pct = s.brightness * 100.f;

        row("brightness");

        if (ImGui::SliderFloat("##brightness", &pct, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            s.brightness = pct / 100.f;
            s.brightnessChanged = true;
        }
    }

    row("hdr");

    if (s.sdrNits > 0.f) {
        float high = fminf(300.f, s.hdrPeakNits);
        float low = fminf(80.f, high);

        // keep room in the cell for the headroom ratio
        ImGui::SetNextItemWidth(-48.f * s.uiScale);
        s.sdrChanged |= ImGui::SliderFloat("##hdr", &s.sdrNits, low, high, "%.0f nits", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        ImGui::TextDisabled("%.1fx", s.hdrPeakNits / s.sdrNits);
    } else {
        ImGui::TextDisabled("off (start with --hdr)");
    }

    ImGui::EndTable();
}

void Dialog::pageColor(Settings& s) {
    if (!beginRows()) {
        return;
    }

    row("night light");

    bool night = ImGui::Checkbox("##nighton", &s.nightOn);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    night |= ImGui::SliderFloat("##night", &s.nightK, 2500.f, 6500.f, "%.0f K", ImGuiSliderFlags_AlwaysClamp);
    s.nightChanged |= night;

    row("neutral");
    s.themeChanged |= ImGui::ColorEdit3("##neutral", &s.neutral.r, ImGuiColorEditFlags_DisplayRGB);
    row("selection");
    s.themeChanged |= ImGui::ColorEdit3("##selection", &s.selection.r, ImGuiColorEditFlags_DisplayRGB);
    ImGui::EndTable();
}

void Dialog::pageAudio(Settings& s) {
    if (s.volume < 0.f) {
        ImGui::TextDisabled("no mixer (sndiod unreachable)");

        return;
    }

    if (!beginRows()) {
        return;
    }

    row("volume");

    bool m = s.volMuted;

    if (ImGui::Checkbox("##mute", &m)) {
        s.volMuted = m;
        s.muteChanged = true;
    }

    float pct = s.volume * 100.f;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);

    if (ImGui::SliderFloat("##volume", &pct, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
        s.volume = pct / 100.f;
        s.volumeChanged = true;
    }

    ImGui::EndTable();
}

void Dialog::pageInput(Settings& s) {
    if (!beginRows()) {
        return;
    }

    if (s.layoutCount) {
        row("layout");

        for (u32 i = 0; i < s.layoutCount; i++) {
            char name[80];
            size_t n = s.layouts[i].length() < sizeof(name) - 1 ? s.layouts[i].length() : sizeof(name) - 1;

            memcpy(name, s.layouts[i].begin(), n);
            name[n] = 0;

            if (ImGui::RadioButton(name, s.layoutActive == i) && s.layoutActive != i) {
                s.layoutSel = i;
                s.layoutChanged = true;
            }
        }

        if (!s.xkbOptions.empty()) {
            row("xkb options");
            ImGui::TextDisabled("%.*s", (int)s.xkbOptions.length(), (const char*)s.xkbOptions.begin());
        }
    }

    row("pointer speed");

    if (s.hasPointer) {
        s.pointerChanged |= ImGui::SliderFloat("##pspeed", &s.pointerSpeed, -1.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    } else {
        ImGui::TextDisabled("no libinput");
    }

    ImGui::EndTable();
}

void Dialog::pageKeys(Settings& s) {
    if (!s.bindingCount) {
        ImGui::TextDisabled("none");

        return;
    }

    if (!ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        return;
    }

    ImGui::TableSetupColumn("chord", ImGuiTableColumnFlags_WidthStretch, 1.f);
    ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 2.f);

    for (size_t i = 0; i < s.bindingCount; i++) {
        const KeyBinding& b = s.bindings[i];

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted((const char*)b.chord.begin(), (const char*)b.chord.end());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted((const char*)b.action.begin(), (const char*)b.action.end());
    }

    ImGui::EndTable();
}

void Dialog::pageNotifications(Settings& s) {
    if (!s.hasDnd) {
        ImGui::TextDisabled("no notification service");

        return;
    }

    if (!beginRows()) {
        return;
    }

    row("do not disturb");

    bool dnd = s.dnd;

    if (ImGui::Checkbox("##dnd", &dnd)) {
        s.dnd = dnd;
        s.dndChanged = true;
    }

    ImGui::EndTable();
}

void Dialog::draw(Settings& s, bool& open) {
    ImGui::SetNextWindowPos(ImVec2(80.f * s.uiScale, 80.f * s.uiScale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(640.f * s.uiScale, 400.f * s.uiScale), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("settings", &open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();

        return;
    }

    if (s.scaleEdit == 0.f) {
        s.scaleEdit = s.uiScale;
    }

    ImGui::BeginChild("nav", ImVec2(130.f * s.uiScale, 0.f));

    for (int i = 0; i < (int)(sizeof(kPages) / sizeof(*kPages)); i++) {
        if (ImGui::Selectable(kPages[i], page == i)) {
            page = i;
        }
    }

    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("page");

    switch (page) {
        case 0:
            pageDisplay(s);
            break;
        case 1:
            pageColor(s);
            break;
        case 2:
            pageAudio(s);
            break;
        case 3:
            pageInput(s);
            break;
        case 4:
            pageKeys(s);
            break;
        case 5:
            pageNotifications(s);
            break;
    }

    ImGui::EndChild();
    ImGui::End();
}

void drawSettings(Composer& c, Settings& s, bool toggle, DialogState** state) {
    s.volumeChanged = false;
    s.muteChanged = false;
    s.brightnessChanged = false;
    s.scaleChanged = false;
    s.sdrChanged = false;
    s.nightChanged = false;
    s.dndChanged = false;
    s.themeChanged = false;
    s.layoutChanged = false;
    s.pointerChanged = false;

    dialog<Dialog>(toggle, state, [&](Dialog& d, bool& open) {
        d.draw(s, open);
    });
}
