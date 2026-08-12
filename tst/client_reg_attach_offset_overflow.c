/* PLAN arithmetic #3: repeated attaches with huge positive and negative
 * offsets. The accumulated buffer offset must saturate, not overflow (UBSan
 * on the sanitizer host would catch a plain i32 sum). */
#include "wl_util.h"

#include <limits.h>

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "attach-offset-overflow", 64, 64, 0xffff0000);

    static const int32_t offs[6] = {INT32_MAX, INT32_MAX, INT32_MAX,
                                    INT32_MIN, INT32_MIN, INT32_MIN};
    for (int i = 0; i < 6; i++) {
        wl_surface_attach(top.surface, wl_solid(64, 64, 0xff00ff00), offs[i], offs[i]);
        wl_surface_damage(top.surface, 0, 0, 64, 64);
        wl_surface_commit(top.surface);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    }

    /* recenter and confirm the connection still works */
    wl_surface_attach(top.surface, wl_solid(64, 64, 0xff0000ff), 0, 0);
    wl_surface_commit(top.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
