#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Scene;
struct Icon;
struct IconStore;

// resolves an icon to an imgui texture valid for the current frame; backed
// by the renderer's gen -> texture cache
struct IconResolver {
    virtual u64 iconTexture(const Icon* icon) = 0;
};

struct Launcher {
    virtual void toggle() = 0;
    virtual bool isOpen() const = 0;

    // one uniform list: row 0 is the query field, then xdg desktop entries;
    // up/down + enter or a mouse click, imgui calls inside, so this runs
    // between NewFrame and Render
    virtual void draw(int screenW, int screenH, float uiScale, IconResolver& texes) = 0;

    static Launcher* create(stl::ObjPool* pool, Scene& scene, IconStore& icons);
};
