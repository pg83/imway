#pragma once

#include <std/mem/obj_pool.h>

struct ev_loop;
struct ev_io;
struct ev_timer;
struct ev_prepare;
struct ev_signal;

struct Session;

// Pool-owned resources: registered into an entity's ObjPool they release
// when the pool dies — LIFO with their siblings, so register in acquisition
// order and dependents die first. An impl is submitted AFTER its constructor
// returns, so cleanups registered inside the constructor run after the
// impl's own destructor: they must be self-contained (captured values, own
// boxes) and must never call back into the impl. One-shot resources go through plain
// registration functions; an interface slot exists only where the owner
// keeps touching the resource (a reopened fd, a rearmed watcher) and must
// reuse one slot instead of leaking dead wrappers into the arena.

// close(fd) at pool death
void pooledFD(stl::ObjPool& pool, int fd);

// a device fd opened through the libseat session: goes back via
// Session::closeDevice, not close()
void pooledSessionFD(stl::ObjPool& pool, Session& session, int fd);

// an fd slot for owners that reopen: reset closes the current fd and takes
// over the next
struct PooledFD {
    virtual int get() const = 0;
    virtual void reset(int newFd) = 0;

    static PooledFD* create(stl::ObjPool& pool, int fd);
};

// watcher slots: start() arms (stopping a previous incarnation first),
// stop() disarms, the pool disarms on death. The callback sees the raw
// watcher, its ->data carries whatever start() was given.
struct PooledEvIo {
    virtual void start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_io*, int), int fd, int events, void* data) = 0;
    virtual void stop() = 0;

    static PooledEvIo* create(stl::ObjPool& pool);
};

struct PooledEvTimer {
    virtual void start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_timer*, int), double after, double repeat, void* data) = 0;
    // init without starting: for debounce timers armed later via again()
    virtual void set(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_timer*, int), double after, double repeat, void* data) = 0;
    virtual void again() = 0;
    virtual void stop() = 0;

    static PooledEvTimer* create(stl::ObjPool& pool);
};

struct PooledEvPrepare {
    virtual void start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_prepare*, int), void* data) = 0;
    virtual void stop() = 0;

    static PooledEvPrepare* create(stl::ObjPool& pool);
};

struct PooledEvSignal {
    virtual void start(struct ev_loop* l, void (*cb)(struct ev_loop*, ev_signal*, int), int signum, void* data) = 0;
    virtual void stop() = 0;

    static PooledEvSignal* create(stl::ObjPool& pool);
};

// a one-off cleanup adopted by the pool: the escape hatch for foreign
// resources without a dedicated registration. Capture by value — by
// pool-death time the registering scope is long gone.
template <typename F>
void pooledGuard(stl::ObjPool& p, F f) {
    struct Guard {
        F f;

        Guard(F g)
            : f(static_cast<F&&>(g))
        {
        }

        ~Guard() noexcept {
            f();
        }
    };

    p.make<Guard>(static_cast<F&&>(f));
}
