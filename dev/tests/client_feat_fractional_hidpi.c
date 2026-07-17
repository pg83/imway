// Feature: fractional-scale under a scaled compositor (--scale 1.5). The
// preferred_scale for a new surface should be 180 (1.5 * 120); the
// compositor currently hardcodes 120 (output scaling not wired up), which
// is why the scenario carries an xfail.

#include "wl_util.h"
#include <fractional-scale-v1-client-protocol.h>

static struct wp_fractional_scale_manager_v1* mgr;
static uint32_t scale;
static int got_scale;

static void preferred(void* d, struct wp_fractional_scale_v1* f, uint32_t s) {
    (void)d; (void)f;
    scale = s;
    got_scale = 1;
}
static const struct wp_fractional_scale_v1_listener frac_listener = {preferred};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_fractional_scale_manager_v1_interface.name))
        mgr = wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!mgr) { fprintf(stderr, "no fractional-scale manager\n"); return 1; }

    struct wl_surface* s = wl_compositor_create_surface(wl_comp);
    struct wp_fractional_scale_v1* f =
        wp_fractional_scale_manager_v1_get_fractional_scale(mgr, s);
    wp_fractional_scale_v1_add_listener(f, &frac_listener, NULL);

    for (int i = 0; i < 100 && !got_scale; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!got_scale) { fprintf(stderr, "no preferred_scale\n"); return 1; }
    printf("preferred_scale=%u\n", scale);
    if (scale != 180) {
        fprintf(stderr, "compositor runs at 1.5 but advertises %u/120\n", scale);
        return 1;
    }
    return 0;
}
