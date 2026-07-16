#pragma once

struct IconResolver;
struct IconStore;
struct Notifier;

// the notification history panel: an opaque-handle dialog listing kept
// notifications newest-first, with a clear button. pure drawing over the
// Notifier store; imgui calls inside, so this runs between NewFrame and
// Render
void drawHistory(Notifier& notifier, IconStore& icons, IconResolver& texes, int screenW, int screenH, float uiScale, bool toggle, void** state);
