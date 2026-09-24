#include "ev_watch.h"

// The bodies of libev's ev_init and ev_TYPE_set, field for field, without
// the `do { } while (0)` they are wrapped in: the macros are statements
// only so they can sit anywhere a statement can, and that loop condition is
// a branch no call can take.
namespace {
    template <class W, class Cb>
    void watcherInit(W* w, Cb cb) {
        ((ev_watcher*)(void*)w)->active = ((ev_watcher*)(void*)w)->pending = 0;
        ev_set_priority(w, 0);
        ev_set_cb(w, cb);
    }
}

void evIoInit(ev_io* w, void (*cb)(struct ev_loop*, ev_io*, int), int fd, int events) {
    watcherInit(w, cb);
    evIoSet(w, fd, events);
}

void evIoSet(ev_io* w, int fd, int events) {
    w->fd = fd;
    w->events = events | EV__IOFDSET;
}

void evTimerInit(ev_timer* w, void (*cb)(struct ev_loop*, ev_timer*, int), ev_tstamp after, ev_tstamp repeat) {
    watcherInit(w, cb);
    evTimerSet(w, after, repeat);
}

void evTimerSet(ev_timer* w, ev_tstamp after, ev_tstamp repeat) {
    ((ev_watcher_time*)w)->at = after;
    w->repeat = repeat;
}

void evPrepareInit(ev_prepare* w, void (*cb)(struct ev_loop*, ev_prepare*, int)) {
    watcherInit(w, cb);
}

void evIdleInit(ev_idle* w, void (*cb)(struct ev_loop*, ev_idle*, int)) {
    watcherInit(w, cb);
}

void evSignalInit(ev_signal* w, void (*cb)(struct ev_loop*, ev_signal*, int), int signum) {
    watcherInit(w, cb);
    w->signum = signum;
}

void evChildInit(ev_child* w, void (*cb)(struct ev_loop*, ev_child*, int), int pid, int trace) {
    watcherInit(w, cb);
    w->pid = pid;
    w->flags = !!trace;
}
