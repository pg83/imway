#include "wl_util.h"

#include <poll.h>
#include <time.h>

static uint64_t monotonic_msec(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(15);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "anr-client", 240, 160, 0xff00ff00);
    printf("anr ready\n");

    /* Stop dispatching long enough to miss two compositor ping periods. */
    usleep(1000000);

    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    printf("anr servicing\n");

    uint64_t end = monotonic_msec() + 1500;

    while (monotonic_msec() < end) {
        struct pollfd pfd = {wl_display_get_fd(wl_dpy), POLLIN, 0};

        wl_display_flush(wl_dpy);

        if (poll(&pfd, 1, 50) < 0) return 1;
        if ((pfd.revents & POLLIN) && wl_display_dispatch(wl_dpy) < 0) return 1;
    }

    printf("anr done\n");
    return 0;
}
