#include "wifi_ui.h"
#include "composer.h"
#include "dialog.h"
#include "scene.h"
#include "wifi.h"
#include "util.h"

#include <imgui.h>

using namespace stl;

namespace {
    // dialog-scoped private state behind DialogState::opaque
    struct Dialog {
        bool fresh = true;
        // raw buffer by imgui InputText contract
        char pass[128] = "";
        // path of the network the passphrase is being typed for; a new
        // request for a different network clears the field
        StringBuilder passPath;

        void draw(Composer& c, bool& open);
    };

    const char* stateLabel(WifiState s) {
        switch (s) {
            case WifiState::unavailable:
                return "no adapter";
            case WifiState::disconnected:
                return "disconnected";
            case WifiState::scanning:
                return "scanning";
            case WifiState::connecting:
                return "connecting";
            case WifiState::connected:
                return "connected";
        }

        return "";
    }
}

void Dialog::draw(Composer& c, bool& open) {
    Wifi& wifi = *c.wifi;
    int screenW = c.scene->outW;
    float uiScale = ImGui::GetStyle().FontScaleMain;

    float w = 320.f * uiScale;

    ImGui::SetNextWindowPos(ImVec2((float)screenW - 8.f, ImGui::GetFrameHeight() + 4.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.f), ImVec2(w, (float)screenW));

    if (ImGui::Begin("##wifi", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
        if (fresh) {
            ImGui::SetWindowFocus();
            fresh = false;
        } else if (!ImGui::IsWindowFocused() && !wifi.passphraseWanted()) {
            open = false;
        }

        auto& hdr = sb();

        hdr << "wi-fi: "_sv << stateLabel(wifi.state());
        ImGui::TextUnformatted(hdr.cStr());
        ImGui::SameLine(w - ImGui::CalcTextSize("scan").x - ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().WindowPadding.x);

        if (ImGui::SmallButton("scan")) {
            wifi.scan();
        }

        ImGui::Separator();

        // passphrase prompt takes over the dialog while iwd waits
        if (wifi.passphraseWanted()) {
            StringView net = wifi.passphraseFor();

            if (sv(passPath) != net) {
                passPath.reset();
                passPath << net;
                pass[0] = 0;
                ImGui::SetKeyboardFocusHere();
            }

            auto& p = sb();

            p << "passphrase for "_sv << net;
            ImGui::TextUnformatted(p.cStr());
            ImGui::SetNextItemWidth(-1.f);

            bool enter = ImGui::InputText("##pass", pass, sizeof(pass), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);

            if (enter) {
                wifi.providePassphrase(StringView(pass));
                pass[0] = 0;
            }

            if (ImGui::Button("cancel")) {
                wifi.cancelPassphrase();
            }

            ImGui::End();

            return;
        }

        int shown = 0;

        wifi.networks([&](WifiNetwork& n) {
            shown++;
            ImGui::PushID(n.path.cStr());

            auto& row = sb();

            row << (n.connected ? "* "_sv : "  "_sv) << sv(n.name);

            if (n.type != "open"_sv) {
                row << "  ["_sv << sv(n.type) << "]"_sv;
            }

            ImVec2 rowMin = ImGui::GetItemRectMin();
            ImVec2 rowMax = ImGui::GetItemRectMax();

            if (ImGui::Selectable(row.cStr(), n.connected) && !n.connected) {
                wifi.connect(sv(n.path));
            }

            rowMin = ImGui::GetItemRectMin();
            rowMax = ImGui::GetItemRectMax();

            // a right-aligned 4-bar signal meter; lit bars scale with strength
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float bw = 3.f * uiScale;
            float gap = 2.f * uiScale;
            float x = rowMax.x - 4.f * (bw + gap);
            float base = rowMax.y - 3.f * uiScale;
            float unit = 4.f * uiScale;
            int lit = (n.strength + 24) / 25; // 0..100 -> 0..4

            for (int b = 0; b < 4; b++) {
                float bh = unit * (float)(b + 1);
                ImU32 col = b < lit ? IM_COL32(200, 200, 210, 255) : IM_COL32(90, 90, 100, 255);

                dl->AddRectFilled(ImVec2(x, base - bh), ImVec2(x + bw, base), col);
                x += bw + gap;
            }

            ImGui::PopID();
        });

        if (!shown) {
            ImGui::TextDisabled("no networks — press scan");
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            open = false;
        }
    }

    ImGui::End();
}

void drawWifi(Composer& c, bool toggle, DialogState** state) {
    dialog<Dialog>(toggle, state, [&](Dialog& d, bool& open) {
        d.draw(c, open);
    });
}
