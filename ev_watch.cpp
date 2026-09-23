#include "ev_watch.h"

void evIoInit(ev_io* w, void (*cb)(struct ev_loop*, ev_io*, int), int fd, int events) {
    ev_io_init(w, cb, fd, events);
}

void evIoSet(ev_io* w, int fd, int events) {
    ev_io_set(w, fd, events);
}

void evTimerInit(ev_timer* w, void (*cb)(struct ev_loop*, ev_timer*, int), ev_tstamp after, ev_tstamp repeat) {
    ev_timer_init(w, cb, after, repeat);
}

void evTimerSet(ev_timer* w, ev_tstamp after, ev_tstamp repeat) {
    ev_timer_set(w, after, repeat);
}

void evPrepareInit(ev_prepare* w, void (*cb)(struct ev_loop*, ev_prepare*, int)) {
    ev_prepare_init(w, cb);
}

void evIdleInit(ev_idle* w, void (*cb)(struct ev_loop*, ev_idle*, int)) {
    ev_idle_init(w, cb);
}

void evSignalInit(ev_signal* w, void (*cb)(struct ev_loop*, ev_signal*, int), int signum) {
    ev_signal_init(w, cb, signum);
}

void evChildInit(ev_child* w, void (*cb)(struct ev_loop*, ev_child*, int), int pid, int trace) {
    ev_child_init(w, cb, pid, trace);
}
