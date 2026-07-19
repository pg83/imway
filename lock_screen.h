#pragma once

struct Composer;
struct DialogState;
struct InputSink;

// Opening is event-side and immediately replaces the renderer's current input
// route. Drawing remains a regular pool-owned ImGui dialog; nullptr state
// means closed.
void openLockOverlay(Composer& c, DialogState** state, InputSink** inputRoute);
void drawLockOverlay(Composer& c, DialogState** state);
void closeLockOverlay(DialogState** state) noexcept;
