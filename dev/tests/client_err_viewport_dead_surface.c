#include "wl_util.h"
#include <viewporter-client-protocol.h>

static struct wp_viewporter* viewporter;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t name) {
    (void)d; (void)r; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!viewporter) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_viewport* viewport = wp_viewporter_get_viewport(viewporter, surface);
    wl_surface_destroy(surface);
    wl_display_roundtrip(wl_dpy);
    wp_viewport_set_destination(viewport, 10, 10);

    return wl_expect_error(wp_viewport_interface.name, WP_VIEWPORT_ERROR_NO_SURFACE);
}
