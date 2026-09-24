#pragma once

struct Composer;
struct DialogState;

// Opening is event-side. Drawing remains a regular pool-owned ImGui dialog;
// nullptr state means closed.
void openLockOverlay(Composer& c, DialogState** state);
void drawLockOverlay(Composer& c, DialogState** state);
void closeLockOverlay(DialogState** state) noexcept;

#ifdef IMWAY_FOR_TESTS
// the PAM conversation contract, held against the lock screen's own
// conversation; the broken parts counted, -1 without PAM
int pamConversationConformance(Composer& c);
#endif
