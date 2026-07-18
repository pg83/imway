#include "desktop_chrome.h"

#include "composer.h"
#include "dock.h"
#include "scene.h"

#include <imgui.h>

#include <stdio.h>
#include <time.h>

using namespace stl;

namespace {
    void drawTop(const DesktopChromeInfo& info, DesktopChromeResult& result) {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiWindowShadowCallback shadow = io.WindowShadowCallback;

        io.WindowShadowCallback = nullptr;
        bool open = ImGui::BeginMainMenuBar();
        io.WindowShadowCallback = shadow;

        if (!open) {
            return;
        }

        time_t now = time(nullptr);
        tm local{};

        localtime_r(&now, &local);

        char clock[32];

        snprintf(clock, sizeof(clock), "%02d.%02d %02d:%02d", local.tm_mday,
            local.tm_mon + 1, local.tm_hour, local.tm_min);

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

        char stats[128];
        int used = snprintf(stats, sizeof(stats), "cpu %d%%  %ld.%ldG", info.cpuPct,
            info.memUsedMb / 1024, info.memUsedMb % 1024 * 10 / 1024);

        if (info.batteryPct >= 0 && used > 0 && (size_t)used < sizeof(stats)) {
            snprintf(stats + used, sizeof(stats) - (size_t)used, "  bat %ld%%%s",
                info.batteryPct, info.batteryCharging ? "+" : "");
        }

        float statsW = ImGui::CalcTextSize(stats).x;
        float statsX = left - statsW - style.ItemSpacing.x * 2.f;

        ImGui::SameLine(statsX);
        ImGui::TextUnformatted(stats);

        if (!info.wifi.empty()) {
            float wifiW = ImGui::CalcTextSize((const char*)info.wifi.begin(), (const char*)info.wifi.end()).x;
            float wifiX = statsX - wifiW - style.ItemSpacing.x * 2.f;

            ImGui::SameLine(wifiX);
            ImGui::TextUnformatted((const char*)info.wifi.begin(), (const char*)info.wifi.end());

            if (ImGui::IsItemClicked()) {
                result.wifi = true;
            }
        }

        ImGui::EndMainMenuBar();
    }
}

void drawDesktopChrome(Composer& c, const DesktopChromeInfo& info, DesktopChromeResult& result) {
    const ImVec4 chrome = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);

    // Both sidebar windows paint the exact same borderless material.  The
    // caller sees one widget; the two rectangles are only ImGui's internal
    // representation of the non-rectangular Г shape.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, chrome);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);

    DockResult dock;

    // Sidebars cut the viewport in call order.  Left-first gives the dock the
    // full height and makes the top bar start at its right edge.
    drawDock(c, dock);
    drawTop(info, result);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

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
