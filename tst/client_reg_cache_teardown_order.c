/* PLAN buffer lifetime #5: a sync subsurface caches a frame callback, then
 * surface, subsurface and finally the whole parent die in that order. */
#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot() || !wl_subcomp) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "cache-teardown", 128, 128, 0xffff0000);

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    wl_subsurface_set_sync(sub);

    wl_surface_frame(child);
    wl_surface_attach(child, wl_solid(32, 32, 0xff00ff00), 0, 0);
    wl_surface_commit(child); /* frame callback parked in the sync cache */
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    wl_surface_destroy(child);
    wl_subsurface_destroy(sub);
    xdg_toplevel_destroy(top.tl);
    xdg_surface_destroy(top.xs);
    wl_surface_destroy(top.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
