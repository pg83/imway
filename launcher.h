#pragma once

#include <std/lib/buffer.h>
#include <std/sys/types.h>

struct Composer;

// the launcher mixes a few compositor actions in among the .desktop programs;
// picking one yields the action (in `action`) instead of a command line
enum class LauncherAction {
    none,
    lockScreen,
    settings,
    notifications,
    inspector,
    colorPicker,
};

// the launcher is a plain imgui widget, not an entity. its state is an
// opaque pool-owned Dialog* in the caller's slot: nullptr = closed. opening
// creates Dialog in its own ObjPool and reads all .desktop entries; closing
// retires that entire pool. *state is also the caller's "is it open" answer.
// toggle flips the dialog on the frame it is passed. row 0 is the query
// field, then the compositor actions and xdg desktop entries; up/down +
// enter or a mouse click. imgui calls inside, so this runs between NewFrame
// and Render. returns true when the user picked something: either `action`
// is set to a compositor action, or the command line is appended to run
// (caller memory, precisely because the dialog dies with the pick)
bool drawLauncher(Composer& c, bool toggle, void** state, stl::Buffer& run,
                  LauncherAction& action, float anchorX = -1.f, float anchorY = -1.f);
