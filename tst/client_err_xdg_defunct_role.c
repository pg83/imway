#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_get_toplevel(xs);
    // Keep the client proxy alive long enough for wl_display_get_protocol_error
    // to resolve the interface of the object that received the fatal error.
    wl_proxy_marshal_flags((struct wl_proxy*)xs, XDG_SURFACE_DESTROY, NULL,
                           wl_proxy_get_version((struct wl_proxy*)xs), 0);

    return wl_expect_error(xdg_surface_interface.name,
                           XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT);
}
