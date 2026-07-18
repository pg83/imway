/* PLAN limits #1 (multi-watch): the same wl_buffer added to an icon several
 * times, then destroyed — all watches fire on one resource; exactly one
 * NO_BUFFER error, no double-free. */
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
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &icon_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!icon_mgr) return 77;

    struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);
    struct wl_buffer* buf = wl_solid(16, 16, 0xff00ff00);
    xdg_toplevel_icon_v1_add_buffer(icon, buf, 1);
    xdg_toplevel_icon_v1_add_buffer(icon, buf, 1);
    xdg_toplevel_icon_v1_add_buffer(icon, buf, 1);
    if (wl_display_roundtrip(wl_dpy) < 0) return 2;

    wl_buffer_destroy(buf);
    return wl_expect_error(xdg_toplevel_icon_v1_interface.name,
                           XDG_TOPLEVEL_ICON_V1_ERROR_NO_BUFFER);
}
