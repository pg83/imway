#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
struct DeviceVk;
struct Keyboard;
struct Composer;
struct Surface;
struct InspectorInfo;

// The GPU backend: frames, targets, textures, capture, the cursor plane.
// The desktop drives it only through this surface; it knows nothing of
// windows, focus or input.
struct Renderer {
    virtual bool screenshot(stl::StringView path) = 0;
    virtual u64 colorIntermediateBytes() = 0;

    // the interactive screenshot (PrintScreen): capture + viewer handoff
    virtual void captureScreenshot() = 0;

    // one-pixel eyedropper into the last composed frame
    virtual bool readPixel(int x, int y, u8& r, u8& g, u8& b) = 0;

    // draw a client surface (and its subsurface piles) at ImGui coordinates
    virtual void drawSurfaceTree(Surface& s, float x, float y) = 0;
    virtual void drawSurfaceTreeOverlay(Surface& s, float x, float y) = 0;
    // the surface's geometry-cropped content stretched into a rect on the
    // given draw list (alt-tab thumbnails); no-op without a texture
    virtual void drawSurfaceRect(Surface& s, void* drawList, float x0, float y0, float x1, float y1) = 0;

    // the cursor plane driver: content per frame, position per input event.
    // cursorPlane returns true when the plane presents the cursor (nothing
    // to compose); false sends the desktop down the software path.
    virtual bool cursorPlane(int kind, Surface* cursorSurface, double x, double y, int hotX, int hotY) = 0;
    virtual void cursorPlaneMove(double x, double y) = 0;
    // gpu-side numbers for the inspector widget
    virtual void inspectorInfo(InspectorInfo& info) = 0;

    static Renderer* create(Composer& c, const DeviceVk& vk, stl::StringView fontPath, float uiScale, int framesLimit);
};
