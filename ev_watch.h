#pragma once

#include <ev.h>

// Watcher setup in one place. libev's ev_TYPE_init / ev_TYPE_set are
// statement macros, `do { ... } while (0)` nested up to three deep, and the
// coverage counters put every one of those constant loop conditions on the
// line that expands them: a condition no run can make true, once per call
// site. These do what the macros do, as plain functions.
void evIoInit(ev_io* w, void (*cb)(struct ev_loop*, ev_io*, int), int fd, int events);
void evIoSet(ev_io* w, int fd, int events);
void evTimerInit(ev_timer* w, void (*cb)(struct ev_loop*, ev_timer*, int), ev_tstamp after, ev_tstamp repeat);
void evTimerSet(ev_timer* w, ev_tstamp after, ev_tstamp repeat);
void evPrepareInit(ev_prepare* w, void (*cb)(struct ev_loop*, ev_prepare*, int));
void evIdleInit(ev_idle* w, void (*cb)(struct ev_loop*, ev_idle*, int));
void evSignalInit(ev_signal* w, void (*cb)(struct ev_loop*, ev_signal*, int), int signum);
void evChildInit(ev_child* w, void (*cb)(struct ev_loop*, ev_child*, int), int pid, int trace);
