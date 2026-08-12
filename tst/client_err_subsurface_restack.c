#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot() || !wl_subcomp) return 2;

    struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
    struct wl_surface* other_parent = wl_compositor_create_surface(wl_comp);
    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_surface* stranger = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* child_sub =
        wl_subcompositor_get_subsurface(wl_subcomp, child, parent);
    wl_subcompositor_get_subsurface(wl_subcomp, stranger, other_parent);
    wl_subsurface_place_above(child_sub, stranger);

    return wl_expect_error(wl_subsurface_interface.name,
                           WL_SUBSURFACE_ERROR_BAD_SURFACE);
}
