#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    wl_surface_set_buffer_scale(surface, 0);

    return wl_expect_error(wl_surface_interface.name, WL_SURFACE_ERROR_INVALID_SCALE);
}
