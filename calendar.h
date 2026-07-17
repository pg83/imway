#pragma once

// the calendar is a plain imgui widget, not an entity. its state is an
// opaque Dialog* in the caller's slot: nullptr = closed. Dialog contains its
// owning ObjPool*, is created in that pool, and retires with the whole pool.
// *state is also the caller's "is it open" answer.
// toggle flips the dialog on the frame it is passed; the dialog also closes
// itself on escape or focus loss. imgui calls inside, so this runs between
// NewFrame and Render
void drawCalendar(int screenW, bool toggle, void** state);
void destroyCalendar(void** state);
