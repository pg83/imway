#include "pooled_ev.h"

#include <ev.h>

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct PooledEvIoImpl: public PooledEvIo {
        struct ev_loop* loop = nullptr;
        ev_io io{};

        ~PooledEvIoImpl() noexcept;

        void start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_io*, int), int fd, int events, void* data) override;
        void stop() override;
    };

    struct PooledEvTimerImpl: public PooledEvTimer {
        struct ev_loop* loop = nullptr;
        ev_timer timer{};

        ~PooledEvTimerImpl() noexcept;

        void start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_timer*, int), double after, double repeat, void* data) override;
        void set(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_timer*, int), double after, double repeat, void* data) override;
        void again() override;
        void stop() override;
    };

    struct PooledEvPrepareImpl: public PooledEvPrepare {
        struct ev_loop* loop = nullptr;
        ev_prepare prep{};

        ~PooledEvPrepareImpl() noexcept;

        void start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_prepare*, int), void* data) override;
        void stop() override;
    };

    struct PooledEvSignalImpl: public PooledEvSignal {
        struct ev_loop* loop = nullptr;
        ev_signal sig{};

        ~PooledEvSignalImpl() noexcept;

        void start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_signal*, int), int signum, void* data) override;
        void stop() override;
    };
}

PooledEvIoImpl::~PooledEvIoImpl() noexcept {
    stop();
}

void PooledEvIoImpl::start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_io*, int), int fd, int events, void* data) {
    stop();
    loop = l;
    ev_io_init(&io, cb, fd, events);
    io.data = data;
    ev_io_start(loop, &io);
}

void PooledEvIoImpl::stop() {
    if (loop) {
        ev_io_stop(loop, &io);
    }
}

PooledEvIo* PooledEvIo::create(ObjPool& pool) {
    return pool.make<PooledEvIoImpl>();
}

PooledEvTimerImpl::~PooledEvTimerImpl() noexcept {
    stop();
}

void PooledEvTimerImpl::start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_timer*, int), double after, double repeat, void* data) {
    stop();
    loop = l;
    ev_timer_init(&timer, cb, after, repeat);
    timer.data = data;
    ev_timer_start(loop, &timer);
}

void PooledEvTimerImpl::set(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_timer*, int), double after, double repeat, void* data) {
    stop();
    loop = l;
    ev_timer_init(&timer, cb, after, repeat);
    timer.data = data;
}

void PooledEvTimerImpl::again() {
    if (loop) {
        ev_timer_again(loop, &timer);
    }
}

void PooledEvTimerImpl::stop() {
    if (loop) {
        ev_timer_stop(loop, &timer);
    }
}

PooledEvTimer* PooledEvTimer::create(ObjPool& pool) {
    return pool.make<PooledEvTimerImpl>();
}

PooledEvPrepareImpl::~PooledEvPrepareImpl() noexcept {
    stop();
}

void PooledEvPrepareImpl::start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_prepare*, int), void* data) {
    stop();
    loop = l;
    ev_prepare_init(&prep, cb);
    prep.data = data;
    ev_prepare_start(loop, &prep);
}

void PooledEvPrepareImpl::stop() {
    if (loop) {
        ev_prepare_stop(loop, &prep);
    }
}

PooledEvPrepare* PooledEvPrepare::create(ObjPool& pool) {
    return pool.make<PooledEvPrepareImpl>();
}

PooledEvSignalImpl::~PooledEvSignalImpl() noexcept {
    stop();
}

void PooledEvSignalImpl::start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_signal*, int), int signum, void* data) {
    stop();
    loop = l;
    ev_signal_init(&sig, cb, signum);
    sig.data = data;
    ev_signal_start(loop, &sig);
}

void PooledEvSignalImpl::stop() {
    if (loop) {
        ev_signal_stop(loop, &sig);
    }
}

PooledEvSignal* PooledEvSignal::create(ObjPool& pool) {
    return pool.make<PooledEvSignalImpl>();
}
