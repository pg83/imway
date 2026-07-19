#pragma once

struct Composer;
struct DialogState;

// the wifi picker: a dialog widget in the opaque-handle style. state is a
// self-owned handle in the caller's slot (nullptr = closed); its ObjPool is
// retired as a unit when the dialog ends. imgui calls inside, so
// this runs between NewFrame and Render
void drawWifi(Composer& c, bool toggle, DialogState** state);
