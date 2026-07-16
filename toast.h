#pragma once

struct IconResolver;
struct IconStore;
struct Notifications;

// notification toasts, stacked below the menu bar at the right edge; the
// whole state lives in Notifications — this is pure drawing plus the click
// = dismiss wire back. imgui calls inside, so this runs between NewFrame
// and Render
void drawToasts(Notifications& notes, IconStore& icons, IconResolver& texes, int screenW, float uiScale);
