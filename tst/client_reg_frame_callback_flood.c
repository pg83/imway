/* PLAN depth #7: thousands of frame callbacks on one surface, surface
 * destroyed before any of them fire. */
#include "wl_util.h"

int main(void) {
    alarm(30);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "callback-flood", 64, 64, 0xffff0000);

    for (int i = 0; i < 4096; i++) wl_surface_frame(top.surface);
    wl_surface_attach(top.surface, wl_solid(64, 64, 0xff00ff00), 0, 0);
    wl_surface_commit(top.surface);

    xdg_toplevel_destroy(top.tl);
    xdg_surface_destroy(top.xs);
    wl_surface_destroy(top.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
