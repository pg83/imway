/* PLAN buffer lifetime #2: commit buffer A, replace it with B, then destroy
 * both in reverse order while B is on screen. The compositor's snapshot keeps
 * showing; nothing may dangle. */
#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "replace-destroy", 64, 64, 0xff808080);

    struct wl_buffer* a = wl_solid(64, 64, 0xffff0000);
    struct wl_buffer* b = wl_solid(64, 64, 0xff00ff00);
    wl_surface_attach(top.surface, a, 0, 0);
    wl_surface_damage(top.surface, 0, 0, 64, 64);
    wl_surface_commit(top.surface);
    wl_surface_attach(top.surface, b, 0, 0);
    wl_surface_damage(top.surface, 0, 0, 64, 64);
    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    wl_buffer_destroy(b);
    wl_buffer_destroy(a);
    wl_surface_commit(top.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
