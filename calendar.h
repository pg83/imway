#pragma once

struct Composer;
struct DialogState;

// the calendar is a plain imgui widget, not an entity. its state is a
// self-owned opaque handle in the caller's slot: nullptr = closed. The
// handle and private widget state retire together with their ObjPool.
// *state is also the caller's "is it open" answer.
// toggle flips the dialog on the frame it is passed; the dialog also closes
// itself on escape or focus loss. imgui calls inside, so this runs between
// NewFrame and Render
void drawCalendar(Composer& c, bool toggle, DialogState** state);
