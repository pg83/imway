#pragma once

struct Composer;
struct ImDrawList;
struct ImVec2;

// The window manager: everything the user sees and touches. Builds the
// whole ImGui frame between the renderer's NewFrame and Render — chrome,
// windows, dialogs, overlays, the cursor — routes input as the first
// InputSink, and owns focus and layout policy. It talks to the GPU only
// through the Renderer interface and the scene; the renderer calls back
// through exactly these two methods.
struct Desktop {
    // the per-frame UI pass, invoked by the renderer inside the ImGui frame
    virtual void build() = 0;

    // cursor shape geometry for the renderer's cursor-plane rasterizer
    virtual void drawCursorShape(ImDrawList* dl, const ImVec2& pos, float scale, int kind) = 0;

    static Desktop* create(Composer& c, float uiScale);
};
