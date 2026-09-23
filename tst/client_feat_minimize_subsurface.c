// A toplevel with a subsurface above it (green, with a yellow grandchild)
// and one below it (blue, sticking out past the parent's bottom right). It
// prints which surface the pointer is on as that changes, and minimizes
// itself on SIGUSR1.

#include "wl_util.h"

#include <poll.h>
#include <signal.h>

static struct wl_toplevel_ctx top;
static volatile sig_atomic_t minimizeRequested;

static void onUsr1(int sig) {
    (void)sig;
    minimizeRequested = 1;
}

static struct wl_surface* child(struct wl_surface* parent, int x, int y, int w, int h, uint32_t color, struct wl_subsurface** sub) {
    struct wl_surface* s = wl_compositor_create_surface(wl_comp);

    *sub = wl_subcompositor_get_subsurface(wl_subcomp, s, parent);
    wl_subsurface_set_position(*sub, x, y);
    wl_subsurface_set_desync(*sub);
    wl_surface_attach(s, wl_solid(w, h, color), 0, 0);
    wl_surface_damage(s, 0, 0, w, h);
    wl_surface_commit(s);

    return s;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGUSR1, onUsr1);
    alarm(55);

    if (wl_boot()) return 1;
    if (!wl_subcomp) { fprintf(stderr, "no subcompositor\n"); return 1; }

    wl_make_toplevel(&top, "subsurf-min", 200, 200, 0xFFFF0000);

    struct wl_subsurface *subGreen, *subGrand, *subBlue;
    struct wl_surface* green = child(top.surface, 20, 20, 100, 100, 0xFF00FF00, &subGreen);
    struct wl_surface* grand = child(green, 10, 10, 30, 30, 0xFFFFFF00, &subGrand);
    struct wl_surface* blue = child(top.surface, 150, 150, 100, 100, 0xFF0000FF, &subBlue);

    wl_subsurface_place_below(subBlue, top.surface);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("subsurfaces mapped\n");

    struct pollfd pfd = {wl_display_get_fd(wl_dpy), POLLIN, 0};
    struct wl_surface* seen = NULL;
    int minimized = 0;

    for (;;) {
        if (minimizeRequested && !minimized) {
            minimized = 1;
            xdg_toplevel_set_minimized(top.tl);
            wl_surface_commit(top.surface);
            printf("minimize requested\n");
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
            printf("pointer on %s\n", seen == green ? "green" : seen == grand ? "grand" : seen == blue ? "blue" : seen == top.surface ? "parent" : "nothing");
        }
    }
}
