#include "wl_util.h"

#include <poll.h>
#include <time.h>

static int frame_done;

static void done(void* data, struct wl_callback* callback, uint32_t msec) {
    (void)data;
    (void)msec;
    frame_done = 1;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_listener = {done};

static uint64_t monotonic_msec(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int dispatch_for(unsigned msec) {
    uint64_t end = monotonic_msec() + msec;

    while (monotonic_msec() < end) {
        struct pollfd pfd = {wl_display_get_fd(wl_dpy), POLLIN, 0};
        uint64_t now = monotonic_msec();
        int timeout = now < end ? (int)(end - now) : 0;

        if (poll(&pfd, 1, timeout) < 0) return -1;
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(wl_dpy) < 0) return -1;
        } else {
            wl_display_dispatch_pending(wl_dpy);
        }
    }

    return 0;
}

int main(void) {
    alarm(15);
    if (wl_boot() || !wl_subcomp) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "invisible-frame-callback", 240, 160, 0xffff0000);

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub =
        wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    struct wl_callback* callback = wl_surface_frame(child);

    wl_callback_add_listener(callback, &frame_listener, NULL);
    wl_surface_commit(child);
    wl_surface_commit(top.surface);
    wl_display_flush(wl_dpy);

    if (dispatch_for(500) < 0 || frame_done) {
        fprintf(stderr, "frame callback fired before the surface had content\n");
        return 1;
    }

    wl_surface_attach(child, wl_solid(80, 60, 0xff00ff00), 0, 0);
    wl_surface_damage(child, 0, 0, 80, 60);
    wl_surface_commit(child);
    wl_surface_commit(top.surface);
    wl_display_flush(wl_dpy);

    uint64_t deadline = monotonic_msec() + 5000;

    while (!frame_done && monotonic_msec() < deadline) {
        if (dispatch_for(50) < 0) return 1;
    }

    if (!frame_done) {
        fprintf(stderr, "frame callback did not fire after presentation\n");
        return 1;
    }

    wl_subsurface_destroy(sub);
    return 0;
}
