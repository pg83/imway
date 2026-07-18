#include "wl_util.h"

static int frame_done;

static void done(void* d, struct wl_callback* callback, uint32_t time) {
    (void)d; (void)time;
    frame_done = 1;
    wl_callback_destroy(callback);
}
static const struct wl_callback_listener frame_listener = {done};

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "health-probe", 32, 32, 0xff00ff00);
    struct wl_callback* callback = wl_surface_frame(top.surface);
    wl_callback_add_listener(callback, &frame_listener, NULL);
    wl_surface_damage(top.surface, 0, 0, 32, 32);
    wl_surface_commit(top.surface);
    while (!frame_done && wl_display_dispatch(wl_dpy) >= 0) {
    }
    return frame_done ? 0 : 1;
}
