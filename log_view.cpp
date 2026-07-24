#include "log_view.h"

#include "log.h"
#include "util.h"
#include "scene.h"
#include "dialog.h"
#include "composer.h"

#include <imgui.h>

using namespace stl;

namespace {
    struct Dialog {
        bool fresh = true;

        void draw(Composer& c, bool& open);
    };
}

void Dialog::draw(Composer& c, bool& open) {
    int screenW = c.scene->outW;
    int screenH = c.scene->outH;
    float uiScale = ImGui::GetStyle().FontScaleMain;
    float w = 640.f * uiScale;
    float h = (float)screenH * 0.5f;

    ImGui::SetNextWindowPos(ImVec2((float)screenW / 2.f, (float)screenH - 8.f * uiScale), ImGuiCond_Appearing, ImVec2(0.5f, 1.f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

    if (ImGui::Begin("log", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
        if (fresh) {
            ImGui::SetWindowFocus();
            fresh = false;
        }

        Log& log = *c.log;
        size_t total = log.histLen();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 1.f * uiScale));

        ImGuiListClipper clipper;

        clipper.Begin((int)total);

        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                StringView line = log.histElem((size_t)i);

                ImGui::TextUnformatted((const char*)line.begin(), (const char*)line.end());
            }
        }

        ImGui::PopStyleVar();

        // follow the tail unless the user scrolled up to read
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f) {
            ImGui::SetScrollHereY(1.f);
        }
    }

    ImGui::End();
}

void drawLogView(Composer& c, bool toggle, DialogState** state) {
    dialog<Dialog>(toggle, state, [&](Dialog& d, bool& open) {
        d.draw(c, open);
    });
}
