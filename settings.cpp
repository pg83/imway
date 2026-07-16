#include "settings.h"

#include <imgui.h>

void drawSettingsMenu(Settings& s) {
    s.volumeChanged = false;
    s.muteChanged = false;
    s.scaleChanged = false;
    s.sdrChanged = false;
    s.nightChanged = false;
    s.open = false;

    if (s.scaleEdit == 0.f) {
        s.scaleEdit = s.uiScale;
    }

    if (!ImGui::BeginMenu("settings")) {
        return;
    }

    s.open = true;

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

    ImGui::SetNextItemWidth(180.f * s.uiScale);
    ImGui::SliderFloat("scale", &s.scaleEdit, 1.f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        s.scale = s.scaleEdit;
        s.scaleChanged = true;
    }

    if (s.sdrNits > 0.f) {
        ImGui::SetNextItemWidth(180.f * s.uiScale);
        s.sdrChanged = ImGui::SliderFloat("hdr", &s.sdrNits, 80.f, 300.f, "%.0f nits", ImGuiSliderFlags_AlwaysClamp);
    } else {
        ImGui::TextDisabled("hdr off (start with --hdr)");
    }

    s.nightChanged = ImGui::Checkbox("##nighton", &s.nightOn);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.f * s.uiScale - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    s.nightChanged |= ImGui::SliderFloat("night", &s.nightK, 2500.f, 6500.f, "%.0f K", ImGuiSliderFlags_AlwaysClamp);

    ImGui::EndMenu();
}
