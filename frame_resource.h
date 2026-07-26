#pragma once

#include <std/mem/obj_pool.h>

// ObjPool already is an ARC. A logical owner and every frame using its
// resources hold the pool itself; its registered cleanups run when the last
// reference retires.
using FrameResource = stl::ObjPool;
using FrameResourceRef = stl::ObjPool::Ref;
