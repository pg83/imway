// Feature: wl_surface.set_input_region. An empty input region makes the
// surface visible but input-transparent — the pointer must never enter it even
// when moved over its pixels.

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
    // empty input region: a region with no rectangles
    struct wl_region* empty = wl_compositor_create_region(wl_comp);
    wl_surface_set_input_region(surface, empty);
    wl_region_destroy(empty);
    wl_surface_attach(surface, wl_solid(400, 300, 0xFFFF0000), 0, 0);
    wl_surface_damage(surface, 0, 0, 400, 300);
    wl_surface_commit(surface);
    printf("client_feat_input_region: ready\n");
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ptr) { fprintf(stderr, "no pointer\n"); return 1; }

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "client_feat_input_region");
    wl_surface_commit(surface);

    // the scenario moves the pointer over our pixels; with an empty input
    // region no enter must ever arrive
    for (int i = 0; i < 150; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        if (wlp_enter_count > 0) {
            fprintf(stderr, "pointer entered an input-transparent surface\n");
            return 1;
        }
        usleep(20000);
    }

    printf("client_feat_input_region: no pointer enter (count=%d)\n", wlp_enter_count);
    printf("client_feat_input_region: ok\n");
    return 0;
}
