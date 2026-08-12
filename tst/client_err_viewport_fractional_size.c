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
    wl_surface_attach(surface, wl_solid(16, 16, 0xff00ff00), 0, 0);
    wp_viewport_set_source(viewport, 0, 0, wl_fixed_from_double(7.5),
                           wl_fixed_from_int(8));
    wl_surface_commit(surface);

    return wl_expect_error(wp_viewport_interface.name, WP_VIEWPORT_ERROR_BAD_SIZE);
}
