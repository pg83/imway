#include "anr_dialog.h"

#include "composer.h"
#include "dialog.h"
#include "scene.h"
#include "util.h"

#include <imgui.h>

using namespace stl;

namespace {
    struct Dialog {
        void draw(Composer& c, Toplevel& t, bool& open);
    };
}

void Dialog::draw(Composer& c, Toplevel& t, bool& open) {
    Scene& scene = *c.scene;

    // a fixed spot: the dialog must not race the window it indicts
    ImGui::SetNextWindowPos(ImVec2((float)scene.outW / 2.f, (float)scene.outH / 3.f), ImGuiCond_Always, ImVec2(0.5f, 0.f));

    if (!ImGui::Begin("not responding", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
        ImGui::End();

        return;
    }

    StringView title = sv(t.title);

    ImGui::Text("\"%.*s\" is not responding.", (int)title.length(), (const char*)title.begin());
    ImGui::TextDisabled("Terminate kills the application; unsaved data is lost.");
    ImGui::Separator();

    if (ImGui::Button("Terminate")) {
        t.terminateRequested = true;
        scene.needsFrame = true;
        open = false;
    }

    ImGui::SameLine();

    if (ImGui::Button("Wait")) {
        open = false;
    }

    ImGui::End();
}

void drawAnrDialog(Composer& c, Weak<Toplevel>& target, bool toggle, DialogState** state) {
    dialog<Dialog>(toggle, state, [&](Dialog& d, bool& open) {
        Toplevel* t = target.get();

        // the target died or recovered: the question answered itself
        if (!t || !t->unresponsive) {
            open = false;

            return;
        }

        d.draw(c, *t, open);
    });
}
