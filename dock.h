#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

struct Composer;
struct Theme;

float dockBarWidth();
float dockIconSize();

// the shared icon button: hover plate, the texture with the dock's inner
// padding, or a fallback — an initial plate when fallbackName is set, the
// 3x3 launcher glyph otherwise. Returns true on a left click.
bool dockIconButton(const Theme& theme, const char* id, u64 texture, float size, bool active, bool attention, stl::StringView fallbackName);

struct DockResult {
    bool launcher = false;
    float launcherX = -1.f;
    float launcherY = -1.f;
};

// Vertical section of desktop_chrome; not a standalone renderer widget.
// It reserves the left viewport edge and applies standard window actions.
void drawDock(Composer& c, DockResult& result);
