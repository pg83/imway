#include "settings.h"
#include "composer.h"
#include "dialog.h"

#include <imgui.h>
#include <math.h>

using namespace stl;

namespace {
    struct Dialog {
        void draw(Settings& s, bool& open);
    };
}

void Dialog::draw(Settings& s, bool& open) {
    ImGui::SetNextWindowSize(ImVec2(340.f * s.uiScale, 340.f * s.uiScale), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("settings", &open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();

        return;
    }

    if (s.scaleEdit == 0.f) {
        s.scaleEdit = s.uiScale;
    }

    if (s.volume >= 0.f) {
        bool m = s.volMuted;

        if (ImGui::Checkbox("##mute", &m)) {
            s.volMuted = m;
            s.muteChanged = true;
        }

        float pct = s.volume * 100.f;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.f * s.uiScale - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);

        if (ImGui::SliderFloat("volume", &pct, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            s.volume = pct / 100.f;
            s.volumeChanged = true;
        }
    }

    if (s.brightness >= 0.f) {
        float pct = s.brightness * 100.f;

        ImGui::SetNextItemWidth(180.f * s.uiScale);

        if (ImGui::SliderFloat("brightness", &pct, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            s.brightness = pct / 100.f;
            s.brightnessChanged = true;
        }
    }

    ImGui::SetNextItemWidth(180.f * s.uiScale);
    ImGui::SliderFloat("scale", &s.scaleEdit, 1.f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        s.scale = s.scaleEdit;
        s.scaleChanged = true;
    }

    if (s.sdrNits > 0.f) {
        float high = fminf(300.f, s.hdrPeakNits);
        float low = fminf(80.f, high);

        ImGui::SetNextItemWidth(180.f * s.uiScale);
        s.sdrChanged = ImGui::SliderFloat("hdr", &s.sdrNits, low, high,
                                          "%.0f nits",
                                          ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        ImGui::TextDisabled("%.1fx", s.hdrPeakNits / s.sdrNits);
    } else {
        ImGui::TextDisabled("hdr off (start with --hdr)");
    }

    s.nightChanged = ImGui::Checkbox("##nighton", &s.nightOn);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.f * s.uiScale - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    s.nightChanged |= ImGui::SliderFloat("night", &s.nightK, 2500.f, 6500.f, "%.0f K", ImGuiSliderFlags_AlwaysClamp);

    if (s.hasDnd) {
        ImGui::Separator();
        s.dndChanged = ImGui::Checkbox("do not disturb", &s.dnd);
    }

    ImGui::SeparatorText("appearance");
    ImGui::SetNextItemWidth(220.f * s.uiScale);
    s.themeChanged |= ImGui::ColorEdit3("neutral", &s.neutral.r, ImGuiColorEditFlags_DisplayRGB);
    ImGui::SetNextItemWidth(220.f * s.uiScale);
    s.themeChanged |= ImGui::ColorEdit3("selection", &s.selection.r, ImGuiColorEditFlags_DisplayRGB);

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

    dialog<Dialog>(toggle, state, [&](Dialog& d, bool& open) {
        d.draw(s, open);
    });
}
