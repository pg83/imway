#include "fence_poll.h"

#include "listener.h"
#include "pooled_ev.h"

#include <std/mem/obj_pool.h>

#include <ev.h>

using namespace stl;

namespace {
    struct FencePollImpl: FencePoll {
        struct ev_loop* loop = nullptr;
        VkDevice device = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Listener* done = nullptr;
        ev_timer* timer = nullptr;
        bool active = false;

        FencePollImpl(ObjPool& pool, struct ev_loop* l, VkDevice d, VkFence f, Listener& listener);

        void arm() override;
        bool armed() const override;
        void cancel() override;
        void poll();
    };

    void fencePollCb(struct ev_loop*, ev_timer* w, int) {
        ((FencePollImpl*)w->data)->poll();
    }
}

FencePollImpl::FencePollImpl(ObjPool& pool, struct ev_loop* l, VkDevice d, VkFence f, Listener& listener)
    : loop(l)
    , device(d)
    , fence(f)
    , done(&listener)
{
    timer = createEvTimer(pool, loop);
    ev_timer_init(timer, fencePollCb, 0.001, 0.001);
    timer->data = this;
}

void FencePollImpl::arm() {
    if (!active) {
        active = true;
        ev_timer_again(loop, timer);
    }
}

bool FencePollImpl::armed() const {
    return active;
}

void FencePollImpl::cancel() {
    if (active) {
        active = false;
        ev_timer_stop(loop, timer);
    }
}

void FencePollImpl::poll() {
    VkResult status = vkGetFenceStatus(device, fence);

    if (status == VK_NOT_READY) {
        return;
    }

    active = false;
    ev_timer_stop(loop, timer);
    done->onListen(&status);
}

FencePoll* FencePoll::create(ObjPool& pool, struct ev_loop* loop, VkDevice device, VkFence fence, Listener& done) {
    return pool.make<FencePollImpl>(pool, loop, device, fence, done);
}
