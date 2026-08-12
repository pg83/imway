#include "wl_util.h"
#include <xdg-activation-v1-client-protocol.h>

static struct xdg_activation_v1* activation;
static char activation_token[128];
static int activation_token_done;
static void token_done(void* d, struct xdg_activation_token_v1* token, const char* value) {
    (void)d; (void)token;
    snprintf(activation_token, sizeof activation_token, "%s", value);
    activation_token_done = 1;
}
static const struct xdg_activation_token_v1_listener token_listener = {token_done};
static void activation_global(void* d, struct wl_registry* registry, uint32_t id,
                              const char* interface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(interface, xdg_activation_v1_interface.name))
        activation = wl_registry_bind(registry, id, &xdg_activation_v1_interface, 1);
}
static void activation_remove(void* d, struct wl_registry* registry, uint32_t id) {
    (void)d; (void)registry; (void)id;
}
static const struct wl_registry_listener activation_registry_listener = {
    activation_global, activation_remove,
};
static int activation_boot(void) {
    if (wl_boot()) return 1;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &activation_registry_listener, NULL);
    return wl_display_roundtrip(wl_dpy) < 0 || !activation;
}
static struct xdg_activation_token_v1* request_token(uint32_t serial,
                                                     struct wl_surface* surface) {
    activation_token_done = 0;
    struct xdg_activation_token_v1* token =
        xdg_activation_v1_get_activation_token(activation);
    xdg_activation_token_v1_add_listener(token, &token_listener, NULL);
    xdg_activation_token_v1_set_serial(token, serial, wl_seat_g);
    if (surface) xdg_activation_token_v1_set_surface(token, surface);
    return token;
}
static int wait_token(void) {
    while (!activation_token_done && wl_display_dispatch(wl_dpy) != -1) {}
    return activation_token_done ? 0 : 1;
}
