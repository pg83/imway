// The window alt-tab has selected goes away while Alt is still held: first
// it unmaps, then, in a second round, it is destroyed. The scenario drives
// the switcher and tells this client when to take the window away.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx a, b;

    wl_make_toplevel(&a, "alt-tab-gone-a", 300, 200, 0xFFFF0000);
    wl_make_toplevel(&b, "alt-tab-gone-b", 300, 200, 0xFF0000FF);
    printf("alt-tab-gone: two toplevels mapped\n");

    // the switcher selects a, the window without the focus
    if (wl_await_file("go-unmap")) return 1;
    wl_surface_attach(a.surface, NULL, 0, 0);
    wl_surface_commit(a.surface);
    wl_display_roundtrip(wl_dpy);
    printf("alt-tab-gone: a unmapped\n");

    // a second round selects b, the one mapped window left
    if (wl_await_file("go-destroy")) return 1;
    xdg_toplevel_destroy(b.tl);
    xdg_surface_destroy(b.xs);
    wl_surface_destroy(b.surface);
    wl_display_roundtrip(wl_dpy);
    printf("alt-tab-gone: b destroyed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
