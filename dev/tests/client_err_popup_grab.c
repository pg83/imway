#include "wl_util.h"

static struct xdg_positioner* positioner(void) {
    struct xdg_positioner* p = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(p, 20, 20);
    xdg_positioner_set_anchor_rect(p, 0, 0, 10, 10);
    return p;
}

int main(void) {
    alarm(10);
    if (wl_boot() || !wl_seat_g) return 2;

    struct wl_surface* root = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* root_xs = xdg_wm_base_get_xdg_surface(wl_wm, root);
    xdg_surface_get_toplevel(root_xs);

    struct wl_surface* parent_surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wl_wm, parent_surface);
    xdg_surface_get_popup(parent_xs, root_xs, positioner());

    struct wl_surface* child_surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* child_xs = xdg_wm_base_get_xdg_surface(wl_wm, child_surface);
    struct xdg_popup* child = xdg_surface_get_popup(child_xs, parent_xs, positioner());
    xdg_popup_grab(child, wl_seat_g, 0);

    return wl_expect_error(xdg_popup_interface.name, XDG_POPUP_ERROR_INVALID_GRAB);
}
