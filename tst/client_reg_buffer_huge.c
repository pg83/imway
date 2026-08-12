/* PLAN arithmetic #10: a large but bounded buffer (4096x4096, 64MB) is either
 * handled or rejected deterministically — the compositor must not die on the
 * allocation. */
#include "wl_util.h"

int main(void) {
    alarm(30);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "buffer-huge", 64, 64, 0xffff0000);

    wl_surface_attach(top.surface, wl_solid(4096, 4096, 0xff00ff00), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 4096, 4096);
    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    /* and back down to a normal size */
    wl_surface_attach(top.surface, wl_solid(64, 64, 0xff0000ff), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 64, 64);
    wl_surface_commit(top.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
