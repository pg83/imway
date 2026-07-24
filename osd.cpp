#include "osd.h"

#include "util.h"

#include <imgui.h>

using namespace stl;

void drawOsd(int screenW, float uiScale, StringView label, float value, bool muted, float alpha) {
    float w = 260.f * uiScale;

    ImGui::SetNextWindowPos(ImVec2((float)screenW / 2.f, ImGui::GetFrameHeight() + 12.f), ImGuiCond_Always, ImVec2(0.5f, 0.f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    if (ImGui::Begin("##osd", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs)) {
        auto& l = sb();

        l << label;

        if (muted) {
            l << " (muted)"_sv;
        }

        ImGui::TextUnformatted(l.cStr());

        if (muted) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        }

        ImGui::ProgressBar(value, ImVec2(-1.f, 6.f * uiScale), "");

        if (muted) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}
