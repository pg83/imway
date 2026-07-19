#include "history.h"
#include "composer.h"
#include "dialog.h"
#include "icon.h"
#include "icon_store.h"
#include "notifier.h"
#include "scene.h"
#include "util.h"

#include <imgui.h>

using namespace stl;

namespace {
    // the panel has no state of its own; its existence is the whole state
    struct Dialog {
        bool fresh = true;

        void draw(Composer& c, bool& open);
    };
}

void Dialog::draw(Composer& c, bool& open) {
    Notifier& notifier = *c.notifier;
    IconStore& icons = *c.icons;
    IconResolver& texes = *c.iconResolver;
    int screenW = c.scene->outW;
    int screenH = c.scene->outH;
    float uiScale = ImGui::GetStyle().FontScaleMain;
    float w = 340.f * uiScale;

    ImGui::SetNextWindowPos(ImVec2((float)screenW - 8.f, ImGui::GetFrameHeight() + 4.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.f), ImVec2(w, (float)screenH * 0.7f));

    if (ImGui::Begin("##history", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
        if (fresh) {
            ImGui::SetWindowFocus();
            fresh = false;
        } else if (!ImGui::IsWindowFocused()) {
            open = false;
        }

        ImGui::TextUnformatted("notifications");
        ImGui::SameLine(w - ImGui::CalcTextSize("clear").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().WindowPadding.x);

        if (ImGui::SmallButton("clear")) {
            notifier.clearHistory();
        }

        ImGui::Separator();

        int shown = 0;

        notifier.history([&](Toast& t) {
            shown++;
            ImGui::PushID((int)t.id);

            float iconSz = ImGui::GetFontSize() * 1.6f;

            if (u64 tex = texes.iconTexture(icons.forIconValue(sv(t.icon)))) {
                ImGui::Image((ImTextureID)tex, ImVec2(iconSz, iconSz));
                ImGui::SameLine();
            }

            ImGui::BeginGroup();

            if (t.critical) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 120, 90, 255));
            }

            ImGui::TextUnformatted(t.summary.cStr());

            if (t.critical) {
                ImGui::PopStyleColor();
            }

            if (!t.body.empty()) {
                ImGui::PushTextWrapPos(w - ImGui::GetStyle().WindowPadding.x * 2);
                ImGui::TextDisabled("%s", t.body.cStr());
                ImGui::PopTextWrapPos();
            }

            ImGui::EndGroup();
            ImGui::PopID();
            ImGui::Separator();
        });

        if (!shown) {
            ImGui::TextDisabled("no notifications");
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            open = false;
        }
    }

    ImGui::End();
}

void drawHistory(Composer& c, bool toggle, DialogState** state) {
    dialog<Dialog>(toggle, state, [&](Dialog& d, bool& open) {
        d.draw(c, open);
    });
}
