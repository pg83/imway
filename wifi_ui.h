#pragma once

struct Wifi;

// the wifi picker: a dialog widget in the opaque-handle style. state is a
// pool-owned Dialog behind the caller's void* slot (nullptr = closed); its
// ObjPool is retired as a unit when the dialog ends. imgui calls inside, so
// this runs between NewFrame and Render
void drawWifi(Wifi& wifi, int screenW, float uiScale, bool toggle, void** state);
void destroyWifi(void** state);
