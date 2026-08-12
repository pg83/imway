// Feature: xdg-decoration. Requesting a toplevel decoration must yield a
// configure with a mode; this compositor is server-side decorated, so even a
// client-side request must be answered with SERVER_SIDE.

#include "wl_util.h"
#include <xdg-decoration-unstable-v1-client-protocol.h>

static struct zxdg_decoration_manager_v1* deco_mgr;
static uint32_t mode;
static int got_mode;

static void deco_configure(void* d, struct zxdg_toplevel_decoration_v1* z, uint32_t m) {
    (void)d; (void)z;
    mode = m; got_mode = 1;
}
static const struct zxdg_toplevel_decoration_v1_listener deco_listener = {deco_configure};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!deco_mgr) { fprintf(stderr, "no xdg-decoration manager\n"); return 1; }

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xsurf = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xsurf);
    struct zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, tl);
    zxdg_toplevel_decoration_v1_add_listener(deco, &deco_listener, NULL);

    // a client that would prefer to draw its own decorations
    zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);

    for (int i = 0; i < 100 && !got_mode; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!got_mode) { fprintf(stderr, "no decoration configure\n"); return 1; }

    printf("client_feat_decoration: mode=%u\n", mode);
    if (mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) {
        fprintf(stderr, "expected SERVER_SIDE\n");
        return 1;
    }
    printf("client_feat_decoration: ok\n");
    return 0;
}
