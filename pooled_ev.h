#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct ev_io;
struct ev_timer;
struct ev_prepare;
struct ev_signal;

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
