/* PLAN arithmetic #7: window geometry with extreme coordinates and a small
 * positive size. The compositor clamps against the view size; no overflow,
 * no death. */
#include "wl_util.h"

#include <limits.h>

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "geometry-extremes", 64, 64, 0xffff0000);

    static const int32_t coords[3] = {INT32_MIN, INT32_MAX, -1};
    for (int i = 0; i < 3; i++) {
        xdg_surface_set_window_geometry(top.xs, coords[i], coords[(i + 1) % 3], 1, 1);
        wl_surface_attach(top.surface, wl_solid(64, 64, 0xff00ff00), 0, 0);
        wl_surface_damage(top.surface, 0, 0, 64, 64);
        wl_surface_commit(top.surface);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    }

    xdg_surface_set_window_geometry(top.xs, INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX);
    wl_surface_commit(top.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
