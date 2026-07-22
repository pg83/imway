#include "wl_util.h"

#include <commit-timing-v1-client-protocol.h>

static struct wp_commit_timing_manager_v1* timing_mgr;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_commit_timing_manager_v1_interface.name))
        timing_mgr = wl_registry_bind(registry, name,
                                      &wp_commit_timing_manager_v1_interface, 1);
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
    if (!timing_mgr) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    wp_commit_timing_manager_v1_get_timer(timing_mgr, surface);
    wp_commit_timing_manager_v1_get_timer(timing_mgr, surface);
    return wl_expect_error(wp_commit_timing_manager_v1_interface.name,
                           WP_COMMIT_TIMING_MANAGER_V1_ERROR_COMMIT_TIMER_EXISTS);
}
