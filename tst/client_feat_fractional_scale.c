// Feature: fractional-scale-v1. Requesting a fractional scale object for a
// surface must yield a preferred_scale event (in 120ths). Headless is scale 1,
// so the compositor should report 120.

#include "wl_util.h"
#include <fractional-scale-v1-client-protocol.h>

static struct wp_fractional_scale_manager_v1* frac_mgr;
static struct wl_toplevel_ctx top;
static uint32_t preferred;
static int got_scale;

static void frac_preferred(void* d, struct wp_fractional_scale_v1* f, uint32_t scale) {
    (void)d; (void)f;
    preferred = scale;
    got_scale = 1;
}
static const struct wp_fractional_scale_v1_listener frac_listener = {frac_preferred};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_fractional_scale_manager_v1_interface.name))
        frac_mgr = wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!frac_mgr) { fprintf(stderr, "no fractional-scale manager\n"); return 1; }

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_fractional_scale_v1* frac =
        wp_fractional_scale_manager_v1_get_fractional_scale(frac_mgr, surface);
    wp_fractional_scale_v1_add_listener(frac, &frac_listener, NULL);

    // map it (the preferred scale is sent for a surface with a role)
    top.surface = surface;
    top.w = 300; top.h = 200; top.color = 0xFFFF0000; top.committed = 0;
    top.xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(top.xs, &wl_tl_xdg_listener, &top);
    top.tl = xdg_surface_get_toplevel(top.xs);
    xdg_toplevel_add_listener(top.tl, &wl_tl_listener, &top);
    xdg_toplevel_set_title(top.tl, "client_feat_fractional_scale");
    wl_surface_commit(surface);

    for (int i = 0; i < 200 && !got_scale; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }

    if (!got_scale) { fprintf(stderr, "no preferred_scale event\n"); return 1; }
    printf("client_feat_fractional_scale: preferred=%u (%.3f)\n", preferred, preferred / 120.0);
    if (preferred != 120) { fprintf(stderr, "expected 120 (scale 1.0)\n"); return 1; }
    printf("client_feat_fractional_scale: ok\n");
    return 0;
}
