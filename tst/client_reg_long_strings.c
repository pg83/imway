/* PLAN limits #2: very long title, app_id, icon name and MIME strings near
 * the wayland message size limit. Nothing may crash or leak; the connection
 * stays healthy. */
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
    if (wl_boot()) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &icon_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "long-strings", 64, 64, 0xffff0000);

    /* just under the 4096-byte wayland message cap */
    static char big[3900];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;

    xdg_toplevel_set_title(top.tl, big);
    xdg_toplevel_set_app_id(top.tl, big);
    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    if (icon_mgr) {
        struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);
        xdg_toplevel_icon_v1_set_name(icon, big);
        xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, top.tl, icon);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    }

    if (wl_ddm && wl_seat_g) {
        struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
        wl_data_source_offer(src, big); /* over the compositor's mime cap: ignored */
        wl_data_source_offer(src, "text/plain");
        wl_data_source_destroy(src);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    }

    /* the window must still be operable */
    xdg_toplevel_set_title(top.tl, "short-again");
    wl_surface_commit(top.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
