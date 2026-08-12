/* PLAN limits #1: toplevel icon resource limits — an oversized square buffer
 * and a flood of buffers on one icon are ignored, never fatal. */
#include "wl_util.h"

#include <xdg-toplevel-icon-v1-client-protocol.h>

static struct xdg_toplevel_icon_manager_v1* icon_mgr;

static void icon_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, xdg_toplevel_icon_manager_v1_interface.name))
        icon_mgr = wl_registry_bind(r, name, &xdg_toplevel_icon_manager_v1_interface, 1);
}
static void icon_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener icon_listener = {icon_global, icon_remove};

int main(void) {
    alarm(30);
    if (wl_boot()) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &icon_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!icon_mgr) return 77;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "icon-limits", 64, 64, 0xffff0000);

    /* oversized: 2048x2048 square, above the compositor's explicit cap */
    struct xdg_toplevel_icon_v1* big = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);
    xdg_toplevel_icon_v1_add_buffer(big, wl_solid(2048, 2048, 0xff00ff00), 1);
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, top.tl, big);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    /* flood: 128 small buffers on one icon; adds beyond the cap are ignored */
    struct xdg_toplevel_icon_v1* many = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);
    for (int i = 0; i < 128; i++)
        xdg_toplevel_icon_v1_add_buffer(many, wl_solid(16, 16, 0xff0000ff), 1);
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, top.tl, many);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
