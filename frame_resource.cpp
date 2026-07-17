#include "frame_resource.h"

#include <std/mem/obj_pool.h>
#include <std/ptr/refcount.h>

using namespace stl;

FrameResource* frameCreate() {
    FrameResource* frame = ObjPool::fromMemoryRaw();

    RefCountOps<ObjPool>::ref(frame);

    return frame;
}

void frameRef(FrameResource* frame) noexcept {
    if (frame) {
        RefCountOps<ObjPool>::ref(frame);
    }
}

void frameUnref(FrameResource* frame) noexcept {
    if (frame) {
        RefCountOps<ObjPool>::unref(frame);
    }
}
