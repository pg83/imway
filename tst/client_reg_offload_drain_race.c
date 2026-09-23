// A window that never stops committing: a full-output wl_shm buffer on
// every frame callback, alternating two colors, so a copy to the GPU is
// almost always in flight or just finished when a screenshot arrives.
#include "wl_util.h"

static struct wl_buffer* buffers[2];
static struct wl_surface* surface;
static int flips;

static void frame_done(void* d, struct wl_callback* cb, uint32_t t);
static const struct wl_callback_listener frame_listener = {frame_done};

static void commit_next(void) {
    struct wl_callback* cb = wl_surface_frame(surface);

    wl_callback_add_listener(cb, &frame_listener, NULL);
    wl_surface_attach(surface, buffers[flips++ & 1], 0, 0);
    wl_surface_damage(surface, 0, 0, 1280, 800);
    wl_surface_commit(surface);
}

static void frame_done(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d; (void)t;
    wl_callback_destroy(cb);
    commit_next();

    if (flips == 4) {
        printf("committing\n");
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    buffers[0] = wl_solid(1280, 800, 0xFFFF0000);
    buffers[1] = wl_solid(1280, 800, 0xFF0000FF);

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "offload-drain", 1280, 800, 0xFF00FF00);
    xdg_toplevel_set_fullscreen(top.tl, NULL);
    surface = top.surface;

    while (!top.committed && wl_display_dispatch(wl_dpy) != -1) {
    }

    commit_next();

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
