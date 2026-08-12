#include "wl_util.h"

#include <xdg-activation-v1-client-protocol.h>

static struct xdg_activation_v1* activation;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, xdg_activation_v1_interface.name))
        activation = wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1);
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
    if (!activation) return 2;

    struct xdg_activation_token_v1* token =
        xdg_activation_v1_get_activation_token(activation);
    xdg_activation_token_v1_commit(token);
    xdg_activation_token_v1_set_app_id(token, "too-late");
    return wl_expect_error(xdg_activation_token_v1_interface.name,
                           XDG_ACTIVATION_TOKEN_V1_ERROR_ALREADY_USED);
}
