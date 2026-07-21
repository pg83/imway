#pragma once

#include "color.h"

#include <stddef.h>

// Accepts ICC v2/v4 RGB Display and ColorSpace matrix-shaper profiles whose
// three TRCs are either sRGB or genuine power curves. The result maps encoded
// client RGB directly into the renderer's linear BT.2020 working space.
bool colorDescriptionFromIcc(const void* data, size_t size, ColorDescription& out);
