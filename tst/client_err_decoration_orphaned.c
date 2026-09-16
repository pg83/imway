/* A decoration object outliving its toplevel is a protocol error: the
 * toplevel must be destroyed after the decoration, never before. */

#include "wl_util.h"

#include <xdg-decoration-unstable-v1-client-protocol.h>

static struct zxdg_decoration_manager_v1* deco_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!deco_mgr) {
        fprintf(stderr, "no xdg-decoration manager\n");
        return 2;
    }

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);

    zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, tl);
    xdg_toplevel_destroy(tl);

    return wl_expect_error(zxdg_toplevel_decoration_v1_interface.name,
                           ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ORPHANED);
}
