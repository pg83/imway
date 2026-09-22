// Helper client for the alt-tab cycle test: a plain toplevel, a wide one
// with a very long title, and a third that is configured but never gets a
// buffer, so it exists without ever mapping. SIGUSR1 destroys the plain one.

#include "wl_util.h"

#include <poll.h>
#include <signal.h>

static struct wl_toplevel_ctx a, b;
static volatile sig_atomic_t dropRequested;

static void onUsr1(int sig) {
    (void)sig;
    dropRequested = 1;
}

static void pendingConfigure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
}

static const struct xdg_surface_listener pendingListener = {pendingConfigure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGUSR1, onUsr1);
    alarm(60);

    if (wl_boot()) return 1;

    wl_make_toplevel(&a, "alt-tab-plain", 300, 200, 0xFFFF0000);
    wl_make_toplevel(&b, "alt-tab-wide", 900, 100, 0xFF0000FF);

    char title[301];

    for (int i = 0; i < 300; i++) {
        title[i] = (char)('a' + i % 26);
    }

    title[300] = 0;
    xdg_toplevel_set_title(b.tl, title);
    wl_surface_commit(b.surface);

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

    xdg_surface_add_listener(xs, &pendingListener, NULL);

    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);

    xdg_toplevel_add_listener(tl, &wl_tl_listener, NULL);
    xdg_toplevel_set_title(tl, "alt-tab-pending");
    xdg_toplevel_set_app_id(tl, "alt-tab-pending");
    wl_surface_commit(surface);
    wl_display_roundtrip(wl_dpy);
    printf("alt-tab cycle: windows up\n");

    struct pollfd pfd = {wl_display_get_fd(wl_dpy), POLLIN, 0};

    for (;;) {
        if (dropRequested == 1) {
            dropRequested = 2;
            xdg_toplevel_destroy(a.tl);
            xdg_surface_destroy(a.xs);
            wl_surface_destroy(a.surface);
            printf("alt-tab cycle: plain window dropped\n");
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
    }
}
