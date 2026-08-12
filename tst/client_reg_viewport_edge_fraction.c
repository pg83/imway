/* PLAN arithmetic #5: viewport source hugging the right/bottom buffer edge
 * with the smallest fractional part: x = 63 + 255/256, w = 1/256 sums to
 * exactly 64.0 and must be accepted. */
#include "wl_util.h"

#include <viewporter-client-protocol.h>

static struct wp_viewporter* viewporter;

static void vp_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                      uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
}
static void vp_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener vp_listener = {vp_global, vp_remove};

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &vp_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!viewporter) {
        fprintf(stderr, "no wp_viewporter\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "viewport-edge", 64, 64, 0xffff0000);

    struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, top.surface);
    /* wl_fixed: 1/256 steps; 63+255/256 + 1/256 == 64.0 exactly */
    wp_viewport_set_source(vp, (63 << 8) | 255, (63 << 8) | 255, 1, 1);
    wp_viewport_set_destination(vp, 16, 16);
    wl_surface_attach(top.surface, wl_solid(64, 64, 0xff00ff00), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 64, 64);
    wl_surface_commit(top.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
