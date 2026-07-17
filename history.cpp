#include "history.h"
#include "dialog_pool.h"
#include "icon_store.h"
#include "launcher.h"
#include "notifier.h"
#include "util.h"

#include <imgui.h>

using namespace stl;

namespace {
    // the panel has no state of its own; its existence is the whole state
    struct Dialog {
        ObjPool* pool = nullptr;
        bool fresh = true;

        Dialog(ObjPool* p)
            : pool(p)
        {
        }

        void draw(Notifier& notifier, IconStore& icons, IconResolver& texes, int screenW, int screenH, float uiScale, bool& open);
    };
}

void Dialog::draw(Notifier& notifier, IconStore& icons, IconResolver& texes, int screenW, int screenH, float uiScale, bool& open) {
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

void drawHistory(Notifier& notifier, IconStore& icons, IconResolver& texes, int screenW, int screenH, float uiScale, bool toggle, void** state) {
    Dialog*& dp = *(Dialog**)state;

    if (toggle) {
        if (dp) {
            dialogPoolDestroy(dp);
        } else {
            dp = dialogPoolCreate<Dialog>();
        }
    }

    if (!dp) {
        return;
    }

    bool open = true;

    dp->draw(notifier, icons, texes, screenW, screenH, uiScale, open);

    if (!open) {
        dialogPoolDestroy(dp);
    }
}

void destroyHistory(void** state) {
    Dialog*& dp = *(Dialog**)state;

    dialogPoolDestroy(dp);
}
