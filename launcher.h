#pragma once

#include <std/str/builder.h>
#include <std/sys/types.h>

struct Icon;
struct IconStore;

// resolves an icon to an imgui texture valid for the current frame; backed
// by the renderer's gen -> texture cache
struct IconResolver {
    virtual u64 iconTexture(const Icon* icon) = 0;
};

// the launcher is a plain imgui widget, not an entity: the caller owns the
// open flag, the widget owns dialog-scoped storage — the .desktop list is
// read anew when the dialog opens and dropped when it closes. row 0 is the
// query field, then the xdg desktop entries; up/down + enter or a mouse
// click. imgui calls inside, so this runs between NewFrame and Render.
// returns true when the user picked something; run receives the command line
bool drawLauncher(int screenW, int screenH, float uiScale, IconStore& icons, IconResolver& texes, bool& open, stl::StringBuilder& run);
