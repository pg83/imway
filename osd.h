#pragma once

#include <std/str/view.h>

// transient value overlay (volume, brightness): pure drawing, the renderer
// owns the timing and calls this only while the osd is alive; alpha 0..1
// drives the fade-out tail. imgui calls inside, so this runs between
// NewFrame and Render
void drawOsd(int screenW, float uiScale, stl::StringView label, float value, bool muted, float alpha);
