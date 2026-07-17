#pragma once

#include <std/sys/types.h>

struct Composer;

// the frame-time ring the renderer feeds and the inspector plots
inline constexpr int kFrameHistory = 120;

// renderer telemetry the inspector displays; the renderer owns the data,
// the widget gets a per-frame view
struct InspectorInfo {
    const float* frameMs = nullptr; // kFrameHistory entries, frameIdx is the write cursor
    int frameIdx = 0;
    u64 textures = 0;
    u64 dmabufCache = 0;
    int hwCursorKind = 0;
    bool hwCursorVisible = false;
};

// the inspector is a plain imgui widget, not an entity. its state is an
// opaque pool-owned Dialog* in the caller's slot: nullptr = closed. *state
// is also the caller's "is it open" answer; closing retires Dialog's entire
// ObjPool. the window's own close button ends it too. imgui calls inside, so
// this runs between NewFrame and Render
void drawInspector(Composer& c, const InspectorInfo& info, bool toggle, void** state);
