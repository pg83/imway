#pragma once

struct Composer;
struct DialogState;

// Opening is event-side. Drawing remains a regular pool-owned ImGui dialog;
// nullptr state means closed.
void openLockOverlay(Composer& c, DialogState** state);
void drawLockOverlay(Composer& c, DialogState** state);
void closeLockOverlay(DialogState** state) noexcept;
