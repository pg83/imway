// Regression: set_desync immediately applies all cached surface state when no
// ancestor remains synchronized. This includes a frame callback even when the
// cached commit did not attach a new buffer.

#include "wl_util.h"

static int frame_done;

static void frame_callback(void* data, struct wl_callback* callback, uint32_t time) {
    (void)data; (void)time;
    frame_done = 1;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_listener = {
    frame_callback,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot() || !wl_subcomp) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "subsurface-desync-frame", 360, 240, 0xFFFF0000);

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub =
        wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    wl_subsurface_set_position(sub, 40, 40);
    wl_surface_attach(child, wl_solid(120, 80, 0xFF00FF00), 0, 0);
    wl_surface_damage(child, 0, 0, 120, 80);
    wl_surface_commit(child);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);

    struct wl_callback* callback = wl_surface_frame(child);
    wl_callback_add_listener(callback, &frame_listener, NULL);
    wl_surface_commit(child); // cached in the default synchronized mode
    wl_subsurface_set_desync(sub); // must apply that cache immediately
    wl_display_flush(wl_dpy);
    printf("subsurface desync frame ready\n");

    for (int i = 0; i < 500 && !frame_done; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(10000);
    }

    if (!frame_done) {
        fprintf(stderr, "cached frame callback was stranded by set_desync\n");
        return 1;
    }

    printf("subsurface desync frame ok\n");
    return 0;
}
