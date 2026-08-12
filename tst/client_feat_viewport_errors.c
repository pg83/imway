// Feature: viewporter error paths. Malformed source/destination rectangles
// must raise the right protocol errors, and a source rectangle larger than the
// buffer must be caught at commit.

#include "wl_util.h"
#include <errno.h>
#include <viewporter-client-protocol.h>

static struct wp_viewporter* viewporter;

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

static int expect_error(const char* iface, uint32_t code) {
    if (wl_display_roundtrip(wl_dpy) >= 0) { fprintf(stderr, "request unexpectedly succeeded\n"); return 1; }
    const struct wl_interface* i = NULL;
    uint32_t id = 0, c = wl_display_get_protocol_error(wl_dpy, &i, &id);
    if (wl_display_get_error(wl_dpy) != EPROTO || !i || strcmp(i->name, iface) || c != code) {
        fprintf(stderr, "unexpected error: iface=%s code=%u (wanted %s %u)\n",
                i ? i->name : "(none)", c, iface, code);
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!viewporter) { fprintf(stderr, "no viewporter\n"); return 2; }

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, surface);

    if (!strcmp(argv[1], "bad-source")) {
        wp_viewport_set_source(vp, 0, 0, wl_fixed_from_int(-5), wl_fixed_from_int(-5));
        return expect_error(wp_viewport_interface.name, WP_VIEWPORT_ERROR_BAD_VALUE);
    }

    if (!strcmp(argv[1], "bad-dest")) {
        wp_viewport_set_destination(vp, 0, 100);
        return expect_error(wp_viewport_interface.name, WP_VIEWPORT_ERROR_BAD_VALUE);
    }

    if (!strcmp(argv[1], "out-of-buffer")) {
        // 100x100 buffer, but the source rectangle covers 200x200
        wl_surface_attach(surface, wl_solid(100, 100, 0xFF00FF00), 0, 0);
        wp_viewport_set_source(vp, 0, 0, wl_fixed_from_int(200), wl_fixed_from_int(200));
        wp_viewport_set_destination(vp, 200, 200);
        wl_surface_commit(surface);
        return expect_error(wp_viewport_interface.name, WP_VIEWPORT_ERROR_OUT_OF_BUFFER);
    }

    return 2;
}
