#pragma once

namespace stl {
    class ObjPool;
}

struct ev_loop;
struct ev_io;
struct ev_timer;
struct ev_prepare;
struct ev_signal;

// Pool-owned libev primitives. Callers use the returned ev_* directly; the
// containing pool only guarantees that an active watcher is stopped on death.
struct PooledEvIo {
    static ev_io* create(stl::ObjPool& pool, struct ev_loop* loop);
};

struct PooledEvTimer {
    static ev_timer* create(stl::ObjPool& pool, struct ev_loop* loop);
};

struct PooledEvPrepare {
    static ev_prepare* create(stl::ObjPool& pool, struct ev_loop* loop);
};

struct PooledEvSignal {
    static ev_signal* create(stl::ObjPool& pool, struct ev_loop* loop);
};
