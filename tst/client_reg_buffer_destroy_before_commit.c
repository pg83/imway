/* PLAN buffer lifetime #1: attach a buffer, destroy the wl_buffer before
 * commit. imway treats the dead pending buffer as a null attach: the toplevel
 * unmaps and a fresh initial configure arrives. The compositor must survive
 * and the client must be able to re-map through the normal dance. */
#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "destroy-before-commit", 64, 64, 0xffff0000);

    struct wl_buffer* doomed = wl_solid(64, 64, 0xff00ff00);
    wl_surface_attach(top.surface, doomed, 0, 0);
    wl_buffer_destroy(doomed);
    wl_surface_damage(top.surface, 0, 0, 64, 64);
    wl_surface_commit(top.surface);

    /* the unmap reset the configure state; drain configures sent before the
     * reset (the ctx acks them without attaching while committed is set) */
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    /* re-map through the full dance: an initial commit without a buffer
     * solicits a fresh configure, the ctx listener acks it and re-attaches */
    top.committed = 0;
    top.color = 0xff0000ff;
    wl_surface_commit(top.surface);
    while (!top.committed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!top.committed) return 1;
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
