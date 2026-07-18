#pragma once

struct Composer;
struct InputSink;

// Opening is event-side and immediately replaces the renderer's current input
// route. Drawing remains a regular pool-owned ImGui dialog; nullptr state
// means closed.
void openLockOverlay(Composer& c, void** state, InputSink** inputRoute);
void drawLockOverlay(Composer& c, void** state);
void closeLockOverlay(void** state) noexcept;
