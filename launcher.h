#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Scene;

// renderer-side texture factory for icons; argb is premultiplied 0xAARRGGBB
struct IconHost {
    virtual u64 iconTexture(const u32* argb, int w, int h) = 0;
};

struct Launcher {
    virtual void toggle() = 0;
    virtual bool isOpen() const = 0;

    // one uniform list: row 0 is the query field, then xdg desktop entries;
    // up/down + enter or a mouse click, imgui calls inside, so this runs
    // between NewFrame and Render
    virtual void draw(int screenW, int screenH, float uiScale) = 0;

    static Launcher* create(stl::ObjPool* pool, Scene& scene, IconHost& icons);
};
