#pragma once

struct Composer;
struct IconResolver;
struct Notifier;

// notification toasts, stacked below the menu bar at the right edge; the
// whole state lives in Notifications — this is pure drawing plus the click
// = dismiss wire back. imgui calls inside, so this runs between NewFrame
// and Render
void drawToasts(Composer& c, Notifier& notes, IconResolver& texes, int screenW, float uiScale);
