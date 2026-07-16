#pragma once

struct Wifi;

// the wifi picker: a dialog widget in the opaque-handle style. state is a
// heap Dialog behind the caller's void* slot (nullptr = closed), newed on
// toggle, deleted when it ends. imgui calls inside, so this runs between
// NewFrame and Render
void drawWifi(Wifi& wifi, int screenW, float uiScale, bool toggle, void** state);
