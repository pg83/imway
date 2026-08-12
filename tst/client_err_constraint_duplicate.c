#include "wl_util.h"

#include <pointer-constraints-unstable-v1-client-protocol.h>

static struct zwp_pointer_constraints_v1* constraints;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, zwp_pointer_constraints_v1_interface.name))
        constraints = wl_registry_bind(registry, name,
                                       &zwp_pointer_constraints_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    alarm(10);
    if (wl_boot() || !wl_ptr) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!constraints) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    zwp_pointer_constraints_v1_lock_pointer(
        constraints, surface, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_pointer_constraints_v1_confine_pointer(
        constraints, surface, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    return wl_expect_error(zwp_pointer_constraints_v1_interface.name,
                           ZWP_POINTER_CONSTRAINTS_V1_ERROR_ALREADY_CONSTRAINED);
}
