// Regression: duplicate per-surface extension objects must draw the
// protocol error the spec names, not a second object. Modes: "viewport"
// (wp_viewporter.get_viewport twice) and "fractional"
// (wp_fractional_scale_manager.get_fractional_scale twice).

#include "wl_util.h"
#include <errno.h>
#include <fractional-scale-v1-client-protocol.h>
#include <viewporter-client-protocol.h>

static struct wp_viewporter* viewporter;
static struct wp_fractional_scale_manager_v1* frac_mgr;

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
    else if (!strcmp(iface, wp_fractional_scale_manager_v1_interface.name))
        frac_mgr = wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

static int expect_error(const char* want_iface, uint32_t want_code) {
    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    if (wl_display_get_error(wl_dpy) != EPROTO) {
        fprintf(stderr, "no protocol error\n");
        return 1;
    }
    const struct wl_interface* iface = NULL;
    uint32_t id;
    uint32_t code = wl_display_get_protocol_error(wl_dpy, &iface, &id);
    if (!iface || strcmp(iface->name, want_iface) || code != want_code) {
        fprintf(stderr, "wrong error: %s code %u, want %s code %u\n",
                iface ? iface->name : "?", code, want_iface, want_code);
        return 1;
    }
    printf("error ok: %s code %u\n", want_iface, want_code);
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char* mode = argc > 1 ? argv[1] : "viewport";
    alarm(20);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    struct wl_surface* s = wl_compositor_create_surface(wl_comp);

    if (!strcmp(mode, "viewport")) {
        if (!viewporter) { fprintf(stderr, "no viewporter\n"); return 1; }
        wp_viewporter_get_viewport(viewporter, s);
        wp_viewporter_get_viewport(viewporter, s);
        wl_display_flush(wl_dpy);
        return expect_error(wp_viewporter_interface.name,
                            WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS);
    }

    if (!frac_mgr) { fprintf(stderr, "no fractional-scale manager\n"); return 1; }
    wp_fractional_scale_manager_v1_get_fractional_scale(frac_mgr, s);
    wp_fractional_scale_manager_v1_get_fractional_scale(frac_mgr, s);
    wl_display_flush(wl_dpy);
    return expect_error(wp_fractional_scale_manager_v1_interface.name,
                        WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS);
}
