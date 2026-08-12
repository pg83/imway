// Feature: single-pixel-buffer + viewporter. A 1x1 solid buffer stretched to
// 200x200 via a viewport dst must fill that area with the solid color.

#include "wl_util.h"
#include <viewporter-client-protocol.h>
#include <single-pixel-buffer-v1-client-protocol.h>

static struct wp_single_pixel_buffer_manager_v1* sp_mgr;
static struct wp_viewporter* viewporter;
static struct wl_surface* surface;
static struct xdg_surface* xs;
static int mapped;

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* s) {
    (void)d; (void)t; (void)w; (void)h; (void)s;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void xs_configure(void* d, struct xdg_surface* x, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(x, serial);
    if (mapped) return;
    mapped = 1;
    // opaque cyan (r=0, g=b=max), premultiplied alpha = max
    struct wl_buffer* buf = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
        sp_mgr, 0, 0xffffffff, 0xffffffff, 0xffffffff);
    struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, surface);
    wp_viewport_set_destination(vp, 200, 200);
    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage(surface, 0, 0, 200, 200);
    wl_surface_commit(surface);
    printf("client_feat_single_pixel: committed 1x1 -> 200x200 cyan\n");
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_single_pixel_buffer_manager_v1_interface.name))
        sp_mgr = wl_registry_bind(r, name, &wp_single_pixel_buffer_manager_v1_interface, 1);
    else if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!sp_mgr || !viewporter) { fprintf(stderr, "no single-pixel / viewporter\n"); return 1; }

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "client_feat_single_pixel");
    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
