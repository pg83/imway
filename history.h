#pragma once

struct Composer;

// the notification history panel: an opaque-handle dialog listing kept
// notifications newest-first, with a clear button. pure drawing over the
// Notifier store; imgui calls inside, so this runs between NewFrame and
// Render
void drawHistory(Composer& c, bool toggle, void** state);
void destroyHistory(void** state);
