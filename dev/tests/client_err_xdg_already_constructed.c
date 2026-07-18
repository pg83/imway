#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_get_toplevel(xs);
    xdg_surface_get_toplevel(xs);

    return wl_expect_error(xdg_surface_interface.name,
                           XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED);
}
