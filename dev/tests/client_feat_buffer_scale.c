// Feature: wl_surface.set_buffer_scale. A 200x200 buffer at scale 2 is a
// logical 100x100 surface — it must render at ~100x100 on screen, not 200x200.

#include "wl_util.h"

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
    wl_surface_set_buffer_scale(surface, 2);
    wl_surface_attach(surface, wl_solid(200, 200, 0xFF00FF00), 0, 0); // green
    wl_surface_damage(surface, 0, 0, 200, 200);
    wl_surface_commit(surface);
    printf("client_feat_buffer_scale: committed 200x200 @ scale 2 (logical 100x100)\n");
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "client_feat_buffer_scale");
    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
