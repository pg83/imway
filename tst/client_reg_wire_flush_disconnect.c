/* Flush a large pipe of perfectly valid requests, then vanish without a
 * roundtrip: the server drains the backlog for a client that is already
 * gone. */
#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    for (int i = 0; i < 1000; i++) {
        struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
        struct wl_region* region = wl_compositor_create_region(wl_comp);
        wl_region_add(region, 0, 0, 8, 8);
        wl_surface_set_input_region(surface, region);
        wl_region_destroy(region);
        wl_surface_commit(surface);
    }
    wl_display_flush(wl_dpy);
    return 0;
}
