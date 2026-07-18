#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot() || !wl_subcomp) return 2;

    struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    wl_subcompositor_get_subsurface(wl_subcomp, child, parent);
    wl_subcompositor_get_subsurface(wl_subcomp, child, parent);

    return wl_expect_error(wl_subcompositor_interface.name,
                           WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE);
}
