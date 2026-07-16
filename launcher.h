#pragma once

#include <std/lib/buffer.h>
#include <std/sys/types.h>

struct Icon;
struct IconStore;

// resolves an icon to an imgui texture valid for the current frame; backed
// by the renderer's gen -> texture cache
struct IconResolver {
    virtual u64 iconTexture(const Icon* icon) = 0;
};

// the launcher mixes a few compositor actions in among the .desktop programs;
// picking one yields the action (in `action`) instead of a command line
enum class LauncherAction {
    none,
    notifications,
    inspector,
    colorPicker,
};

// the launcher is a plain imgui widget, not an entity. its state is an
// opaque handle in the caller's slot: nullptr = closed, the widget news the
// dialog there on open (reading all .desktop entries) and deletes it when
// the dialog ends — *state is also the caller's "is it open" answer.
// toggle flips the dialog on the frame it is passed. row 0 is the query
// field, then the compositor actions and xdg desktop entries; up/down +
// enter or a mouse click. imgui calls inside, so this runs between NewFrame
// and Render. returns true when the user picked something: either `action`
// is set to a compositor action, or the command line is appended to run
// (caller memory, precisely because the dialog dies with the pick)
bool drawLauncher(int screenW, int screenH, float uiScale, IconStore& icons, IconResolver& texes, bool toggle, void** state, stl::Buffer& run, LauncherAction& action);
