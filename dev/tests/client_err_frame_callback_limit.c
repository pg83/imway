#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);

    for (int i = 0; i < 4097; i++) {
        wl_surface_frame(surface);
    }

    return wl_expect_error(wl_display_interface.name,
                           WL_DISPLAY_ERROR_NO_MEMORY);
}
