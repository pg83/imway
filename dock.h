#pragma once

struct Composer;

float dockBarWidth();

struct DockResult {
    bool launcher = false;
    float launcherX = -1.f;
    float launcherY = -1.f;
};

// Vertical section of desktop_chrome; not a standalone renderer widget.
// It reserves the left viewport edge and applies standard window actions.
void drawDock(Composer& c, DockResult& result);
