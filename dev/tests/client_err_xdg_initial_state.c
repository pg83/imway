#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    wl_surface_attach(surface, wl_solid(16, 16, 0xff00ff00), 0, 0);
    xdg_wm_base_get_xdg_surface(wl_wm, surface);

    return wl_expect_error(xdg_wm_base_interface.name,
                           XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE);
}
