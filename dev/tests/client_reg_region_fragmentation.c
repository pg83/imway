/* PLAN depth #4: heavy region fragmentation — one big rectangle minus a grid
 * of strips — installed as the input region. Bounded time (the alarm) and no
 * unbounded growth on the compositor side. */
#include "wl_util.h"

int main(void) {
    alarm(30);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "region-frag", 128, 128, 0xffff0000);

    struct wl_region* region = wl_compositor_create_region(wl_comp);
    wl_region_add(region, 0, 0, 1024, 1024);
    for (int i = 0; i < 256; i++) wl_region_subtract(region, i * 4 + 1, 0, 1, 1024);
    for (int i = 0; i < 256; i++) wl_region_subtract(region, 0, i * 4 + 1, 1024, 1);

    wl_surface_set_input_region(top.surface, region);
    wl_region_destroy(region);
    wl_surface_attach(top.surface, wl_solid(64, 64, 0xff00ff00), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 64, 64);
    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    /* replace it with a fresh fragmented region a few times over */
    for (int round = 0; round < 4; round++) {
        struct wl_region* r = wl_compositor_create_region(wl_comp);
        wl_region_add(r, 0, 0, 1024, 1024);
        for (int i = 0; i < 128; i++) wl_region_subtract(r, i * 8, i * 8, 4, 4);
        wl_surface_set_input_region(top.surface, r);
        wl_region_destroy(r);
        wl_surface_commit(top.surface);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    }
    return 0;
}
