#pragma once

namespace stl {
    class ObjPool;
}

struct ev_io;
struct ev_loop;
struct ev_timer;
struct ev_signal;
struct ev_prepare;

// Pool-owned libev primitives. Callers use the returned ev_* directly; the
// containing pool only guarantees that an active watcher is stopped on death.
ev_io* createEvIo(stl::ObjPool& pool, struct ev_loop* loop);
ev_timer* createEvTimer(stl::ObjPool& pool, struct ev_loop* loop);
ev_signal* createEvSignal(stl::ObjPool& pool, struct ev_loop* loop);
ev_prepare* createEvPrepare(stl::ObjPool& pool, struct ev_loop* loop);
