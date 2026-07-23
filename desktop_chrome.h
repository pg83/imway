#pragma once

#include <std/str/view.h>

struct Composer;

struct DesktopChromeInfo {
    // app_id of the focused toplevel, first element of the bar; empty when
    // nothing is focused — nothing is printed
    stl::StringView focusedAppId;
    stl::StringView layout;
    stl::StringView wifi;
    // < 0 hides the widget: no battery, or the machine runs on mains
    long batteryPct = -1;
};

struct DesktopChromeResult {
    bool launcher = false;
    bool calendar = false;
    bool wifi = false;
    float launcherX = -1.f;
    float launcherY = -1.f;
};

// The Unity-like desktop chrome is one public widget.  Internally its two
// rectangular viewport sidebars are submitted left-first, then top: the dock
// owns the upper-left corner and the menu bar plugs into its right edge.
void drawDesktopChrome(Composer& c, const DesktopChromeInfo& info, DesktopChromeResult& result);
