// An xdg_surface is no role: its wl_surface can still become a cursor, and
// then asking the xdg_surface for a toplevel (argv[1] "toplevel") or a popup
// ("popup") is refused as a second role.

#include "wl_util.h"

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (argc < 2 || wl_boot() || !wl_ptr) return 2;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "cursor-role", 240, 160, 0xffff0000);
    printf("cursor-role ready\n");
    while (wlp_focus != top.surface && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, surface, 0, 0);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "set_cursor on a surface with an xdg_surface was refused\n");
        return 1;
    }
    printf("cursor-role cursor set\n");

    if (!strcmp(argv[1], "popup")) {
        struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
        xdg_positioner_set_size(pos, 40, 30);
        xdg_positioner_set_anchor_rect(pos, 0, 0, 10, 10);
        xdg_surface_get_popup(xs, top.xs, pos);
    } else {
        xdg_surface_get_toplevel(xs);
    }

    return wl_expect_error(xdg_surface_interface.name, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED);
}
