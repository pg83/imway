#include "offload_job.h"

#include "composer.h"
#include "listener.h"
#include "pooled_ev.h"

#include <ev.h>

#include <std/mem/obj_pool.h>
#include <std/sys/event_fd.h>
#include <std/thr/pool.h>

using namespace stl;

namespace {
    struct OffloadJobImpl: OffloadJob {
        Composer* c = nullptr;
        void (*work)(void*) = nullptr;
        void* self = nullptr;
        Listener* done = nullptr;
        EventFD fd;
        ev_io* io = nullptr;
        bool busy = false;
        bool again = false;

        OffloadJobImpl(Composer& comp, void (*w)(void*), void* s, Listener& listener);

        void run() override;
        bool inFlight() const override;
        void join() override;
        void retired();
    };

    void offloadJobCb(struct ev_loop*, ev_io* w, int) {
        ((OffloadJobImpl*)w->data)->retired();
    }
}

OffloadJobImpl::OffloadJobImpl(Composer& comp, void (*w)(void*), void* s, Listener& listener)
    : c(&comp)
    , work(w)
    , self(s)
    , done(&listener)
{
    io = createEvIo(*comp.pool, comp.loop);
    ev_io_init(io, offloadJobCb, fd.fd(), EV_READ);
    io->data = this;
    ev_io_start(comp.loop, io);
}

void OffloadJobImpl::run() {
    if (busy) {
        again = true;

        return;
    }

    busy = true;
    c->offload->submit([this] {
        work(self);
        fd.signal();
    });
}

bool OffloadJobImpl::inFlight() const {
    return busy;
}

void OffloadJobImpl::join() {
    if (busy) {
        c->offload->join();
    }
}

void OffloadJobImpl::retired() {
    fd.drain();
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
    return c.pool->make<OffloadJobImpl>(c, work, self, done);
}
