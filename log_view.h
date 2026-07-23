#pragma once

struct Composer;
struct DialogState;

// the log panel: an opaque-handle dialog showing the tail of the in-memory
// log (Composer::log history), auto-following new lines. imgui calls
// inside, so this runs between NewFrame and Render
void drawLogView(Composer& c, bool toggle, DialogState** state);
