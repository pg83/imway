#pragma once

#include <vulkan/vulkan.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct Listener;

// A millisecond poll of a Vulkan fence from the event loop: arm() starts
// the timer and the listener fires once, on the loop, with a VkResult*
// when the fence leaves VK_NOT_READY. The fence itself is left untouched —
// the owner resets it before the next submission. cancel() disarms
// without firing (teardown, a mode change failing the consumers).
struct FencePoll {
    virtual void arm() = 0;
    virtual bool armed() const = 0;
    virtual void cancel() = 0;

    static FencePoll* create(stl::ObjPool& pool, struct ev_loop* loop, VkDevice device, VkFence fence, Listener& done);
};
