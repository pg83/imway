#pragma once

#include <imgui.h>

// an accent halo behind a box (the dock's focused icon). Self-contained:
// the sprite is a flat rounded core with a gaussian skirt baked into the
// font atlas — the core hides under the glowing box, so the visible rim
// starts at full brightness right at its edge; a centered gaussian would
// bury the peak under the icon and leave only a dim tail showing.

// registers and rasterizes the sprite; call once after fonts are set up
void bakeGlow(ImFontAtlas* atlas);

// one tinted quad, the sprite core stretched under the box grown by reach
// (negative pulls the rim inside the box). The halo may spill past the
// host window's clip — light does. No-op before bakeGlow.
void drawGlow(ImDrawList* dl, ImVec2 min, ImVec2 max, float reach, ImU32 color);
