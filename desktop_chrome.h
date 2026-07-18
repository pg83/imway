#pragma once

#include <std/str/view.h>

struct Composer;

struct DesktopChromeInfo {
    stl::StringView layout;
    stl::StringView wifi;
    int cpuPct = 0;
    long memUsedMb = 0;
    long batteryPct = -1;
    bool batteryCharging = false;
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
