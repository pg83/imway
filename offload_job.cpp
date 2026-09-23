#include "offload_job.h"

#include "pooled.h"
#include "composer.h"
#include "listener.h"
#include "ev_watch.h"

#include <std/thr/pool.h>
#include <std/sys/atomic.h>
#include <std/mem/obj_pool.h>
#include <std/sys/event_fd.h>

#include <ev.h>

using namespace stl;

namespace {
    struct OffloadJobImpl: OffloadJob {
        Composer* c = nullptr;
        void (*work)(void*) = nullptr;
        void* self = nullptr;
        Listener* done = nullptr;
        EventFD fd;
        ev_io* io = nullptr;
        // passes the worker finished, and those the loop has run the
        // completion of: a wakeup with nothing new between them is stale
        u32 completion = 0;
        u32 retiredPasses = 0;
        bool busy = false;
        bool again = false;

        OffloadJobImpl(Composer& comp, ObjPool& owner, void (*w)(void*), void* s, Listener& listener);
        ~OffloadJobImpl() noexcept;

        void run() override;
        void join() override;
        void drain() override;
        void retired();
    };

    void offloadJobCb(struct ev_loop*, ev_io* w, int) {
        ((OffloadJobImpl*)w->data)->retired();
    }
}

OffloadJobImpl::OffloadJobImpl(Composer& comp, ObjPool& owner, void (*w)(void*), void* s, Listener& listener)
    : c(&comp)
    , work(w)
    , self(s)
    , done(&listener)
{
    io = owner.make<ev_io>();
    struct ev_loop* heldLoop = comp.loop;
    ev_io* heldIo = io;

    pooledGuard(owner, [heldLoop, heldIo] {
        ev_io_stop(heldLoop, heldIo);
    });
    evIoInit(io, offloadJobCb, fd.fd(), EV_READ);
    io->data = this;
    ev_io_start(comp.loop, io);
}

OffloadJobImpl::~OffloadJobImpl() noexcept {
    join();
}

void OffloadJobImpl::run() {
    if (busy) {
        again = true;

        return;
    }

    busy = true;
    c->offload->submit([this] {
        work(self);
        stdAtomicAddAndFetch(&completion, 1, MemoryOrder::Release);
        fd.signal();
    });
}

void OffloadJobImpl::join() {
    if (busy) {
        c->offload->join();
    }
}

void OffloadJobImpl::drain() {
    while (busy) {
        c->offload->join();
        // the io watcher may still report this pass's wakeup, collected
        // before the drain: retired() takes it for the stale one it is
        retired();
    }
}

void OffloadJobImpl::retired() {
    fd.drain();

    u32 finished = stdAtomicFetch(&completion, MemoryOrder::Acquire);

    // libev collects the eventfd's wakeup before it runs any callback of
    // the iteration; when an earlier one drained the pass (a screenshot
    // taking the shm copy in flight), the wakeup still arrives, for a pass
    // already retired, or one started since that is still running
    if (finished == retiredPasses) {
        return;
    }

    retiredPasses = finished;
    busy = false;

    bool rerun = again;

    again = false;
    done->onListen(nullptr);

    // the listener may have started its own pass; the coalesced one is
    // only owed when it did not
    if (rerun && !busy) {
        run();
    }
}

OffloadJob* OffloadJob::create(Composer& c, void (*work)(void*), void* self, Listener& done) {
    return create(c, *c.pool, work, self, done);
}

OffloadJob* OffloadJob::create(Composer& c, ObjPool& owner, void (*work)(void*), void* self, Listener& done) {
    return owner.make<OffloadJobImpl>(c, owner, work, self, done);
}
