#include "wl_util.h"

#include <keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h>

static struct zwp_keyboard_shortcuts_inhibit_manager_v1* manager;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name))
        manager = wl_registry_bind(
            registry, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    alarm(10);
    if (wl_boot() || !wl_seat_g) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!manager) return 2;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
        manager, surface, wl_seat_g);
    zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
        manager, surface, wl_seat_g);
    return wl_expect_error(zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name,
                           ZWP_KEYBOARD_SHORTCUTS_INHIBIT_MANAGER_V1_ERROR_ALREADY_INHIBITED);
}
