// Three overlapping 400x400 toplevels mapped bottom, middle, top. It prints
// which one the pointer is on as that changes ("pointer on bottom|middle|
// top|nothing"); SIGUSR1 minimizes the top one, SIGUSR2 unmaps the middle
// one with a null buffer.

#include "wl_util.h"

#include <poll.h>
#include <signal.h>

static struct wl_toplevel_ctx bottom, middle, top;
static volatile sig_atomic_t minimizeRequested, unmapRequested;

static void onUsr1(int sig) {
    (void)sig;
    minimizeRequested = 1;
}

static void onUsr2(int sig) {
    (void)sig;
    unmapRequested = 1;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGUSR1, onUsr1);
    signal(SIGUSR2, onUsr2);
    alarm(55);

    if (wl_boot()) return 1;

    wl_make_toplevel(&bottom, "handoff-bottom", 400, 400, 0xFFFF0000);
    wl_make_toplevel(&middle, "handoff-middle", 400, 400, 0xFF00FF00);
    wl_make_toplevel(&top, "handoff-top", 400, 400, 0xFF0000FF);
    printf("windows mapped\n");

    struct pollfd pfd = {wl_display_get_fd(wl_dpy), POLLIN, 0};
    struct wl_surface* seen = NULL;
    int minimized = 0, unmapped = 0;

    for (;;) {
        if (minimizeRequested && !minimized) {
            minimized = 1;
            xdg_toplevel_set_minimized(top.tl);
            wl_surface_commit(top.surface);
            printf("minimize requested\n");
        }

        if (unmapRequested && !unmapped) {
            unmapped = 1;
            wl_surface_attach(middle.surface, NULL, 0, 0);
            wl_surface_commit(middle.surface);
            printf("unmap requested\n");
        }

        wl_display_flush(wl_dpy);

        if (wl_display_prepare_read(wl_dpy) != 0) {
            if (wl_display_dispatch_pending(wl_dpy) < 0) return 0;
            continue;
        }

        if (poll(&pfd, 1, 100) > 0) {
            if (wl_display_read_events(wl_dpy) < 0) return 0;
        } else {
            wl_display_cancel_read(wl_dpy);
        }

        if (wl_display_dispatch_pending(wl_dpy) < 0) return 0;

        if (wlp_focus != seen) {
            seen = wlp_focus;
            printf("pointer on %s\n", seen == bottom.surface ? "bottom" : seen == middle.surface ? "middle" : seen == top.surface ? "top" : "nothing");
        }
    }
}
