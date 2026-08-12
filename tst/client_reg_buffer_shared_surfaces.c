/* PLAN buffer lifetime #3: one shm buffer committed to two surfaces at once,
 * destroyed only after both commits. */
#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx one, two;
    wl_make_toplevel(&one, "shared-one", 64, 64, 0xffff0000);
    wl_make_toplevel(&two, "shared-two", 64, 64, 0xff0000ff);

    struct wl_buffer* shared = wl_solid(64, 64, 0xff00ff00);
    wl_surface_attach(one.surface, shared, 0, 0);
    wl_surface_damage(one.surface, 0, 0, 64, 64);
    wl_surface_commit(one.surface);
    wl_surface_attach(two.surface, shared, 0, 0);
    wl_surface_damage(two.surface, 0, 0, 64, 64);
    wl_surface_commit(two.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    wl_buffer_destroy(shared);
    wl_surface_commit(one.surface);
    wl_surface_commit(two.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
