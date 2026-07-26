#include "desktop_chrome.h"

#include "dock.h"
#include "util.h"
#include "scene.h"
#include "composer.h"
#include "dbus_menu.h"
#include "dbus_menu_ui.h"
#include "imgui_wm.h"

#include <time.h>

using namespace stl;

namespace {
    void drawOuterShadow(Composer& c, bool dock, bool top) {
        if (!c.settings.windowShadows.get() || !dock || !top || c.settings.dockPosition.get() != DockPosition::left) {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 pos = viewport->Pos;
        ImVec2 size = viewport->Size;
        float dockW = dockBarWidth(c);
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

    void drawOuterBorder(Composer& c, ImDrawList& draw) {
        if (!c.settings.dockVisible.get() || c.settings.dockPosition.get() != DockPosition::left) {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiStyle& style = ImGui::GetStyle();
        ImVec2 pos = viewport->Pos;
        ImVec2 size = viewport->Size;
        float dockW = dockBarWidth(c);
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

    void drawTop(Composer& c, const DesktopChromeInfo& info, DesktopChromeResult& result) {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiWindowShadowCallback shadow = io.WindowShadowCallback;

        io.WindowShadowCallback = nullptr;
        bool open = ImGui::BeginMainMenuBar();
        io.WindowShadowCallback = shadow;

        if (!open) {
            return;
        }

        if (c.settings.topBarAppId.get() && !info.focusedAppId.empty()) {
            ImGui::TextUnformatted((const char*)info.focusedAppId.begin(), (const char*)info.focusedAppId.end());
        }

        if (c.settings.topBarGlobalMenu.get() && info.globalMenu && info.globalMenu->ready) {
            drawDBusMenuBar(c, *info.globalMenu);
        }

        time_t now = time(nullptr);
        tm local{};

        localtime_r(&now, &local);

        char clock[128];
        const char* format;

        if (c.settings.clockLocale.get()) {
            format = c.settings.clockShowDate.get() ? c.settings.clock24Hour.get() ? c.settings.clockShowSeconds.get() ? "%x %H:%M:%S" : "%x %H:%M" : c.settings.clockShowSeconds.get() ? "%x %I:%M:%S %p" : "%x %I:%M %p" : c.settings.clock24Hour.get() ? c.settings.clockShowSeconds.get() ? "%H:%M:%S" : "%H:%M" : c.settings.clockShowSeconds.get() ? "%I:%M:%S %p" : "%I:%M %p";
        } else {
            format = c.settings.clockShowDate.get() ? c.settings.clock24Hour.get() ? c.settings.clockShowSeconds.get() ? "%d.%m %H:%M:%S" : "%d.%m %H:%M" : c.settings.clockShowSeconds.get() ? "%d.%m %I:%M:%S %p" : "%d.%m %I:%M %p" : c.settings.clock24Hour.get() ? c.settings.clockShowSeconds.get() ? "%H:%M:%S" : "%H:%M" : c.settings.clockShowSeconds.get() ? "%I:%M:%S %p" : "%I:%M %p";
        }

        strftime(clock, sizeof(clock), format, &local);

        const ImGuiStyle& style = ImGui::GetStyle();
        float clockW = ImGui::CalcTextSize(clock).x;
        float x = ImGui::GetWindowWidth() - clockW - style.ItemSpacing.x;

        ImGui::SetCursorPosX(x);
        ImGui::TextUnformatted(clock);

        if (ImGui::IsItemClicked()) {
            result.calendar = true;
        }

        float left = x;

        if (c.settings.topBarLayout.get() && !info.layout.empty()) {
            float w = ImGui::CalcTextSize((const char*)info.layout.begin(), (const char*)info.layout.end()).x;

            left = x - w - style.ItemSpacing.x * 2.f;
            ImGui::SameLine(left);
            ImGui::TextUnformatted((const char*)info.layout.begin(), (const char*)info.layout.end());
        }

        if (c.settings.topBarBattery.get() != BatteryDisplay::never && info.batteryPct >= 0) {
            auto& stats = sb();

            stats << "bat "_sv << info.batteryPct << "%"_sv;

            float statsW = ImGui::CalcTextSize(stats.cStr()).x;

            left -= statsW + style.ItemSpacing.x * 2.f;
            ImGui::SameLine(left);
            ImGui::TextUnformatted(stats.cStr());
        }

        if (c.settings.topBarWifi.get() && !info.wifi.empty()) {
            float wifiW = ImGui::CalcTextSize((const char*)info.wifi.begin(), (const char*)info.wifi.end()).x;
            float wifiX = left - wifiW - style.ItemSpacing.x * 2.f;

            ImGui::SameLine(wifiX);
            ImGui::TextUnformatted((const char*)info.wifi.begin(), (const char*)info.wifi.end());

            if (ImGui::IsItemClicked()) {
                result.wifi = true;
            }
        }

        drawOuterBorder(c, *ImGui::GetWindowDrawList());
        ImGui::EndMainMenuBar();
    }
}

void drawDesktopChrome(Composer& c, const DesktopChromeInfo& info, DesktopChromeResult& result) {
    const ImVec4 chrome = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 mouse = ImGui::GetMousePos();
    DockPosition position = c.settings.dockPosition.get();
    float extent = dockBarWidth(c);
    bool nearDock = position == DockPosition::left ? mouse.x <= viewport->Pos.x + extent : position == DockPosition::right ? mouse.x >= viewport->Pos.x + viewport->Size.x - extent : position == DockPosition::top ? mouse.y <= viewport->Pos.y + extent : mouse.y >= viewport->Pos.y + viewport->Size.y - extent;
    bool dockVisible = c.settings.dockVisible.get() && (!c.settings.dockAutoHide.get() || nearDock);
    bool topVisible = c.settings.topBarVisible.get();

    drawOuterShadow(c, dockVisible, topVisible);

    // Both sidebar windows paint the exact same borderless material.  The
    // caller sees one widget; the two rectangles are only ImGui's internal
    // representation of the non-rectangular Г shape.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, chrome);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, chrome);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);

    DockResult dock;

    if (dockVisible) {
        drawDock(c, dock);
    }

    if (topVisible) {
        drawTop(c, info, result);
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    result.launcher = dock.launcher;
    result.launcherX = dock.launcherX;
    result.launcherY = dock.launcherY;

    for (size_t i = 0; i < sizeof(result.launchApp); i++) {
        result.launchApp[i] = dock.launchApp[i];

        if (!dock.launchApp[i]) {
            break;
        }
    }

    Scene& scene = *c.scene;

    scene.workX = (int)viewport->WorkPos.x;
    scene.workY = (int)viewport->WorkPos.y;
    scene.workW = (int)viewport->WorkSize.x;
    scene.workH = (int)viewport->WorkSize.y;
}
