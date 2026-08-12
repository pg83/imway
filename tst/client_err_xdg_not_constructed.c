#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    xdg_wm_base_get_xdg_surface(wl_wm, surface);
    wl_surface_commit(surface);

    return wl_expect_error(xdg_surface_interface.name, XDG_SURFACE_ERROR_NOT_CONSTRUCTED);
}
