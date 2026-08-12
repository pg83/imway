// libev smoke test: timer fires, loop exits.

#include <cstdio>

#include <ev.h>

static void on_timer(struct ev_loop* loop, ev_timer*, int) {
    ev_break(loop, EVBREAK_ALL);
}

int main() {
    struct ev_loop* loop = ev_default_loop(0);
    ev_timer t;
    ev_timer_init(&t, on_timer, 0.01, 0.);
    ev_timer_start(loop, &t);
    ev_run(loop, 0);
    std::printf("ok (backend=%x)\n", ev_backend(loop));
    return 0;
}
