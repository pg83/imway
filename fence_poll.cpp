#include "fence_poll.h"

#include "pooled.h"
#include "listener.h"
#include "composer.h"
#include "chaos_monkey.h"
#include "ev_watch.h"

#include <std/mem/obj_pool.h>

#include <ev.h>

using namespace stl;

namespace {
    struct FencePollImpl: FencePoll {
        Composer* c = nullptr;
        VkDevice device = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Listener* done = nullptr;
        ev_timer* timer = nullptr;
        bool active = false;

        FencePollImpl(ObjPool& pool, Composer& comp, VkDevice d, VkFence f, Listener& listener);

        void arm() override;
        bool armed() const override;
        void cancel() override;
        void poll();
    };

    void fencePollCb(struct ev_loop*, ev_timer* w, int) {
        ((FencePollImpl*)w->data)->poll();
    }
}

FencePollImpl::FencePollImpl(ObjPool& pool, Composer& comp, VkDevice d, VkFence f, Listener& listener)
    : c(&comp)
    , device(d)
    , fence(f)
    , done(&listener)
{
    timer = pool.make<ev_timer>();
    struct ev_loop* heldLoop = c->loop;
    ev_timer* heldTimer = timer;

    pooledGuard(pool, [heldLoop, heldTimer] {
        ev_timer_stop(heldLoop, heldTimer);
    });
    evTimerInit(timer, fencePollCb, 0.001, 0.001);
    timer->data = this;
}

// both owners arm only a poll that is not armed: the capture records a
// copy only while its poll is idle, and the screenshot submits once per
// capture it is not busy with
void FencePollImpl::arm() {
    active = true;
    ev_timer_again(c->loop, timer);
}

bool FencePollImpl::armed() const {
    return active;
}

void FencePollImpl::cancel() {
    active = false;
    ev_timer_stop(c->loop, timer);
}

void FencePollImpl::poll() {
    VkResult status = c->chaos->readbackPoll(vkGetFenceStatus(device, fence));

    if (status == VK_NOT_READY) {
        return;
    }

    active = false;
    ev_timer_stop(c->loop, timer);
    done->onListen(&status);
}

FencePoll* FencePoll::create(ObjPool& pool, Composer& c, VkDevice device, VkFence fence, Listener& done) {
    return pool.make<FencePollImpl>(pool, c, device, fence, done);
}
