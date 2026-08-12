/* PLAN depth #5: a fragmented input region cycled through pending, sync
 * cache and current state of a subsurface, repeatedly. */
#include "wl_util.h"

int main(void) {
    alarm(30);
    if (wl_boot() || !wl_subcomp) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "region-cache", 128, 128, 0xffff0000);

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    wl_subsurface_set_sync(sub);

    for (int round = 0; round < 8; round++) {
        struct wl_region* r = wl_compositor_create_region(wl_comp);
        wl_region_add(r, 0, 0, 512, 512);
        for (int i = 0; i < 128; i++) wl_region_subtract(r, i * 4 + 1, 0, 1, 512);
        for (int i = 0; i < 64; i++) wl_region_subtract(r, 0, i * 8 + 1, 512, 2);
        wl_surface_set_input_region(child, r);
        wl_region_destroy(r);
        wl_surface_attach(child, wl_solid(16, 16, 0xff00ff00), 0, 0);
        wl_surface_commit(child); /* pending -> cache */
        wl_surface_commit(top.surface); /* cache -> current */
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    }

    wl_surface_destroy(child);
    wl_subsurface_destroy(sub);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
