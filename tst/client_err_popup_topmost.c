#include "wl_util.h"

static struct xdg_positioner* positioner(void) {
    struct xdg_positioner* p = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(p, 20, 20);
    xdg_positioner_set_anchor_rect(p, 0, 0, 10, 10);
    return p;
}

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;

    struct wl_surface* root = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* root_xs = xdg_wm_base_get_xdg_surface(wl_wm, root);
    xdg_surface_get_toplevel(root_xs);

    struct wl_surface* first_surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* first_xs = xdg_wm_base_get_xdg_surface(wl_wm, first_surface);
    struct xdg_popup* first = xdg_surface_get_popup(first_xs, root_xs, positioner());

    struct wl_surface* second_surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* second_xs = xdg_wm_base_get_xdg_surface(wl_wm, second_surface);
    xdg_surface_get_popup(second_xs, first_xs, positioner());
    xdg_popup_destroy(first);

    return wl_expect_error(xdg_wm_base_interface.name,
                           XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP);
}
