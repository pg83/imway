/* PLAN buffer lifetime #10: resize/recreate cycles where the old buffer dies
 * before the new one was ever shown. */
#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "recreate-cycle", 64, 64, 0xff808080);

    struct wl_buffer* prev = NULL;
    for (int i = 0; i < 10; i++) {
        struct wl_buffer* next = wl_solid(48 + 8 * i, 48 + 8 * i, 0xff000000 | (i * 25 << 8));
        wl_surface_attach(top.surface, next, 0, 0);
        wl_surface_damage(top.surface, 0, 0, 48 + 8 * i, 48 + 8 * i);
        wl_surface_commit(top.surface);
        if (prev) wl_buffer_destroy(prev);
        prev = next;
    }
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    wl_buffer_destroy(prev);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
