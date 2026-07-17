#pragma once

namespace stl {
    class ObjPool;
}

// ObjPool already is an ARC. A logical owner and every frame using its
// resources hold the pool itself; its registered cleanups run when the last
// reference retires.
using FrameResource = stl::ObjPool;

FrameResource* frameCreate();
void frameRef(FrameResource*) noexcept;
void frameUnref(FrameResource*) noexcept;
