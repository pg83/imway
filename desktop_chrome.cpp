#include "desktop_chrome.h"

#include "composer.h"
#include "dock.h"
#include "scene.h"

#include <imgui.h>

#include <stdio.h>
#include <time.h>

using namespace stl;

namespace {
    void drawOuterShadow() {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 pos = viewport->Pos;
        ImVec2 size = viewport->Size;
        float dockW = dockBarWidth();
        float topH = ImGui::GetFrameHeight();
        ImDrawList* background = ImGui::GetBackgroundDrawList(viewport);

        // Submit both rectangular shadows before either material rectangle.
        // Their internal halves are subsequently covered by chrome itself;
        // only the union's outer shadow remains visible.
        if (io.WindowShadowCallback) {
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;

            io.WindowShadowCallback(background, pos, ImVec2(dockW, size.y), 0.f, flags, io.WindowShadowCallbackUserData);
            io.WindowShadowCallback(background, ImVec2(pos.x + dockW, pos.y), ImVec2(size.x - dockW, topH), 0.f, flags, io.WindowShadowCallbackUserData);
        }
    }

    void drawOuterBorder(ImDrawList& draw) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec2 pos = viewport->Pos;
        ImVec2 size = viewport->Size;
        float dockW = dockBarWidth();
        float topH = ImGui::GetFrameHeight();

        // One six-segment outline describes the Г union.  It lives in the
        // top sidebar's ordinary draw list: after both chrome materials, but
        // before the client windows submitted later in the frame.
        ImVec2 outline[] = {
            pos,
            ImVec2(pos.x + size.x, pos.y),
            ImVec2(pos.x + size.x, pos.y + topH),
            ImVec2(pos.x + dockW, pos.y + topH),
            ImVec2(pos.x + dockW, pos.y + size.y),
            ImVec2(pos.x, pos.y + size.y),
        };

        draw.PushClipRectFullScreen();
        draw.AddPolyline(outline, 6, ImGui::GetColorU32(ImGuiCol_Border), ImDrawFlags_Closed, style.WindowBorderSize);
        draw.PopClipRect();
    }

    void drawTop(const DesktopChromeInfo& info, DesktopChromeResult& result) {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiWindowShadowCallback shadow = io.WindowShadowCallback;

        io.WindowShadowCallback = nullptr;
        bool open = ImGui::BeginMainMenuBar();
        io.WindowShadowCallback = shadow;

        if (!open) {
            return;
        }

        if (!info.focusedAppId.empty()) {
            ImGui::TextUnformatted((const char*)info.focusedAppId.begin(), (const char*)info.focusedAppId.end());
        }

        time_t now = time(nullptr);
        tm local{};

        localtime_r(&now, &local);

        char clock[32];

        snprintf(clock, sizeof(clock), "%02d.%02d %02d:%02d", local.tm_mday, local.tm_mon + 1, local.tm_hour, local.tm_min);

        const ImGuiStyle& style = ImGui::GetStyle();
        float clockW = ImGui::CalcTextSize(clock).x;
        float x = ImGui::GetWindowWidth() - clockW - style.ItemSpacing.x;

        ImGui::SetCursorPosX(x);
        ImGui::TextUnformatted(clock);

        if (ImGui::IsItemClicked()) {
            result.calendar = true;
        }

        float left = x;

        if (!info.layout.empty()) {
            float w = ImGui::CalcTextSize((const char*)info.layout.begin(), (const char*)info.layout.end()).x;

            left = x - w - style.ItemSpacing.x * 2.f;
            ImGui::SameLine(left);
            ImGui::TextUnformatted((const char*)info.layout.begin(), (const char*)info.layout.end());
        }

        if (info.batteryPct >= 0) {
            char stats[32];

            snprintf(stats, sizeof(stats), "bat %ld%%", info.batteryPct);

            float statsW = ImGui::CalcTextSize(stats).x;

            left -= statsW + style.ItemSpacing.x * 2.f;
            ImGui::SameLine(left);
            ImGui::TextUnformatted(stats);
        }

        if (!info.wifi.empty()) {
            float wifiW = ImGui::CalcTextSize((const char*)info.wifi.begin(), (const char*)info.wifi.end()).x;
            float wifiX = left - wifiW - style.ItemSpacing.x * 2.f;

            ImGui::SameLine(wifiX);
            ImGui::TextUnformatted((const char*)info.wifi.begin(), (const char*)info.wifi.end());

            if (ImGui::IsItemClicked()) {
                result.wifi = true;
            }
        }

        drawOuterBorder(*ImGui::GetWindowDrawList());
        ImGui::EndMainMenuBar();
    }
}

void drawDesktopChrome(Composer& c, const DesktopChromeInfo& info, DesktopChromeResult& result) {
    const ImVec4 chrome = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);

    drawOuterShadow();

    // Both sidebar windows paint the exact same borderless material.  The
    // caller sees one widget; the two rectangles are only ImGui's internal
    // representation of the non-rectangular Г shape.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, chrome);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, chrome);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);

    DockResult dock;

    // Sidebars cut the viewport in call order.  Left-first gives the dock the
    // full height and makes the top bar start at its right edge.
    drawDock(c, dock);
    drawTop(info, result);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    result.launcher = dock.launcher;
    result.launcherX = dock.launcherX;
    result.launcherY = dock.launcherY;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    Scene& scene = *c.scene;

    scene.workX = (int)viewport->WorkPos.x;
    scene.workY = (int)viewport->WorkPos.y;
    scene.workW = (int)viewport->WorkSize.x;
    scene.workH = (int)viewport->WorkSize.y;
}
