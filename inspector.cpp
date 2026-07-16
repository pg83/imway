#include "inspector.h"
#include "scene.h"
#include "util.h"

#include <imgui.h>

using namespace stl;

namespace {
    // dialog-scoped state: the dialog's existence is the whole state — the
    // opaque handle behind the caller's void* slot
    struct Dialog {
        // pure drawing: state transitions stay in drawInspector, the only
        // outward sign is the open flag dropping
        void draw(Scene& scene, const InspectorInfo& info, float uiScale, bool& open);
    };
}

void Dialog::draw(Scene& scene, const InspectorInfo& info, float uiScale, bool& open) {
    ImGui::SetNextWindowSize(ImVec2(440.f * uiScale, 400.f * uiScale), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("inspector", &open, ImGuiWindowFlags_NoDocking)) {
        float last = info.frameMs[(info.frameIdx + kFrameHistory - 1) % kFrameHistory];

        ImGui::PlotLines("##ft", info.frameMs, kFrameHistory, info.frameIdx, nullptr, 0.f, 8.f, ImVec2(-1.f, 44.f * uiScale));

        auto& l = sb();

        l << "frame "_sv << (i64)scene.framesDone << ", "_sv << (i64)last << "."_sv << (i64)(last * 10) % 10 << " ms, textures "_sv << info.textures << ", dmabuf cache "_sv << info.dmabufCache;
        ImGui::TextUnformatted(l.cStr());
        l.reset();
        l << "kb -> "_sv << (scene.kbCaptured ? "ui" : "client") << ", ptr -> "_sv << (scene.ptrCaptured ? "ui" : "client") << ", focus: "_sv << (scene.focusedToplevel ? sv(scene.focusedToplevel->title) : "-"_sv);
        ImGui::TextUnformatted(l.cStr());
        l.reset();
        l << "cursor shape "_sv << (i64)scene.cursorShape << ", hw kind "_sv << info.hwCursorKind << (info.hwCursorVisible ? ", visible"_sv : ", hidden"_sv) << (scene.pointerLocked ? ", LOCKED"_sv : ""_sv) << (scene.pointerConfined ? ", CONFINED"_sv : ""_sv);
        ImGui::TextUnformatted(l.cStr());
        ImGui::Separator();

        for (Toplevel* t : scene.toplevels) {
            StringView title = sv(t->title);

            l.reset();
            l << (title.length() > 200 ? title.prefix(200) : title) << "###insp"_sv << (u64)t->id;

            if (ImGui::TreeNode(l.cStr())) {
                Surface* s = t->surface;

                l.reset();
                l << "app_id "_sv << (!t->appId.empty() ? sv(t->appId) : "-"_sv) << (t->mapped ? ", mapped"_sv : ""_sv) << (t->csd ? ", csd"_sv : ", ssd"_sv) << (t->fullscreen ? ", fullscreen"_sv : ""_sv);
                ImGui::TextUnformatted(l.cStr());

                if (s) {
                    l.reset();
                    l << "buffer "_sv << s->width << "x"_sv << s->height << " @"_sv << s->bufferScale << (s->dmabuf ? " dmabuf"_sv : " shm"_sv) << ", geom "_sv << s->geomX() << ","_sv << s->geomY() << " "_sv << s->geomW() << "x"_sv << s->geomH();
                    ImGui::TextUnformatted(l.cStr());
                    l.reset();
                    l << "subsurfaces "_sv << (u64)(s->stackBelow.length() + s->stackAbove.length()) << ", pos "_sv << (i64)s->imgX << ","_sv << (i64)s->imgY;
                    ImGui::TextUnformatted(l.cStr());
                }

                ImGui::TreePop();
            }
        }

        if (scene.popups.length()) {
            l.reset();
            l << (u64)scene.popups.length() << " popup(s)"_sv;
            ImGui::TextUnformatted(l.cStr());
        }
    }

    ImGui::End();
}

void drawInspector(Scene& scene, const InspectorInfo& info, float uiScale, bool toggle, void** state) {
    Dialog*& dp = *(Dialog**)state;

    if (toggle) {
        if (dp) {
            delete dp;
            dp = nullptr;
        } else {
            dp = new Dialog();
        }
    }

    if (!dp) {
        return;
    }

    bool open = true;

    dp->draw(scene, info, uiScale, open);

    if (!open) {
        delete dp;
        dp = nullptr;
    }
}
