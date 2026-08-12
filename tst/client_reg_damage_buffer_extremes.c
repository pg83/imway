/* PLAN arithmetic #2: wl_surface.damage_buffer with INT32_MIN/MAX in every
 * argument. */
#include "wl_util.h"

#include <limits.h>

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "damage-buffer-extremes", 64, 64, 0xffff0000);

    static const int32_t vals[4] = {INT32_MIN, -1, INT32_MAX, 1};
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++) {
            wl_surface_attach(top.surface, wl_solid(64, 64, 0xff00ff00), 0, 0);
            wl_surface_damage_buffer(top.surface, vals[a], vals[b], vals[(a + 1) % 4],
                                     vals[(b + 1) % 4]);
            wl_surface_damage_buffer(top.surface, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
            wl_surface_commit(top.surface);
            if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        }
    return 0;
}
