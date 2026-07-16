#include "toast.h"
#include "icon_store.h"
#include "launcher.h"
#include "notifications.h"
#include "util.h"

#include <imgui.h>

using namespace stl;

void drawToasts(Notifications& notes, IconStore& icons, IconResolver& texes, int screenW, float uiScale) {
    float w = 320.f * uiScale;
    float y = ImGui::GetFrameHeight() + 8.f;
    u32 clicked = 0;

    notes.active([&](Toast& t) {
        ImGui::SetNextWindowPos(ImVec2((float)screenW - 8.f, y), ImGuiCond_Always, ImVec2(1.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(w, 0.f), ImGuiCond_Always);

        auto& label = sb();

        label << "##toast"_sv << (u64)t.id;

        if (t.critical) {
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(220, 90, 60, 255));
        }

        if (ImGui::Begin(label.cStr(), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            float iconSz = ImGui::GetFontSize() * 2.f;

            if (u64 tex = texes.iconTexture(icons.forIconValue(sv(t.icon)))) {
                ImGui::Image((ImTextureID)tex, ImVec2(iconSz, iconSz));
                ImGui::SameLine();
            }

            ImGui::BeginGroup();
            ImGui::TextUnformatted(t.summary.cStr());

            if (!t.body.empty()) {
                ImGui::PushTextWrapPos(w - ImGui::GetStyle().WindowPadding.x * 2);
                ImGui::TextDisabled("%s", t.body.cStr());
                ImGui::PopTextWrapPos();
            }

            ImGui::EndGroup();

            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                clicked = t.id;
            }

            y += ImGui::GetWindowHeight() + 6.f;
        }

        ImGui::End();

        if (t.critical) {
            ImGui::PopStyleColor();
        }
    });

    if (clicked) {
        notes.dismiss(clicked);
    }
}
