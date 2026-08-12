/* PLAN arithmetic #6: a viewport source rectangle whose x + width sits at
 * the wl_fixed limit (~8388607.996) is far outside any real buffer and must
 * raise OUT_OF_BUFFER at commit, without overflowing on the way. */
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
    if (wl_boot()) return 2;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &vp_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!viewporter) return 2;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "viewport-limit", 64, 64, 0xffff0000);

    struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, top.surface);
    /* both x and width at the wl_fixed maximum */
    wp_viewport_set_source(vp, INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX);
    wp_viewport_set_destination(vp, 16, 16);
    wl_surface_attach(top.surface, wl_solid(64, 64, 0xff00ff00), 0, 0);
    wl_surface_commit(top.surface);

    return wl_expect_error(wp_viewport_interface.name, WP_VIEWPORT_ERROR_OUT_OF_BUFFER);
}
