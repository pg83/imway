#include "wl_util.h"

#include <color-management-v1-client-protocol.h>

static struct wp_color_manager_v1* manager;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        manager = wl_registry_bind(registry, name, &wp_color_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!manager) return 2;

    struct wp_image_description_creator_params_v1* params =
        wp_color_manager_v1_create_parametric_creator(manager);
    wl_proxy_marshal_flags(
        (struct wl_proxy*)params, WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_CREATE,
        &wp_image_description_v1_interface,
        wl_proxy_get_version((struct wl_proxy*)params), 0, NULL);
    return wl_expect_error(wp_image_description_creator_params_v1_interface.name,
                           WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INCOMPLETE_SET);
}
