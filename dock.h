#pragma once

struct Composer;

struct DockResult {
    bool launcher = false;
    float launcherX = -1.f;
    float launcherY = -1.f;
};

// Unity-style left sidebar. It reserves viewport work area and mutates the
// selected Toplevel's public compositor state for standard window actions.
void drawDock(Composer& c, DockResult& result);
