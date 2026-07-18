/* PLAN depth #8: thousands of short-lived surfaces with a full
 * create/commit/destroy cycle, plus periodic full toplevel map dances. */
#include "wl_util.h"

int main(void) {
    alarm(60);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "surface-churn", 64, 64, 0xffff0000);

    struct wl_buffer* dot = wl_solid(4, 4, 0xff00ff00);
    for (int i = 0; i < 2048; i++) {
        struct wl_surface* s = wl_compositor_create_surface(wl_comp);
        wl_surface_attach(s, dot, 0, 0);
        wl_surface_commit(s);
        wl_surface_destroy(s);
        if ((i & 255) == 255 && wl_display_roundtrip(wl_dpy) < 0) return 1;
    }

    for (int i = 0; i < 16; i++) {
        struct wl_toplevel_ctx t;
        wl_make_toplevel(&t, "churn-window", 32, 32, 0xff0000ff);
        xdg_toplevel_destroy(t.tl);
        xdg_surface_destroy(t.xs);
        wl_surface_destroy(t.surface);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    }
    return 0;
}
