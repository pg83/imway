#include "wl_util.h"
#include <xdg-activation-v1-client-protocol.h>

static struct xdg_activation_v1* activation;
static char token_value[128];
static int token_done;

static void token_done_cb(void* d, struct xdg_activation_token_v1* token,
                          const char* value) {
    (void)d; (void)token;
    snprintf(token_value, sizeof token_value, "%s", value);
    token_done = 1;
}
static const struct xdg_activation_token_v1_listener token_listener = {
    token_done_cb,
};
static void activation_global(void* d, struct wl_registry* registry, uint32_t id,
                              const char* interface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(interface, xdg_activation_v1_interface.name))
        activation = wl_registry_bind(registry, id,
                                      &xdg_activation_v1_interface, 1);
}
static void activation_remove(void* d, struct wl_registry* registry, uint32_t id) {
    (void)d; (void)registry; (void)id;
}
static const struct wl_registry_listener activation_registry_listener = {
    activation_global, activation_remove,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 1;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &activation_registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!activation) return 1;

    struct wl_toplevel_ctx first, second;
    wl_make_toplevel(&first, "activation-old-focus", 200, 130, 0xffff0000);
    uint32_t old_serial = wlk_enter_serial;
    wl_make_toplevel(&second, "activation-new-focus", 200, 130, 0xff00ff00);
    if (!old_serial || wlk_focus != second.surface) return 1;

    struct xdg_activation_token_v1* token =
        xdg_activation_v1_get_activation_token(activation);
    xdg_activation_token_v1_add_listener(token, &token_listener, NULL);
    xdg_activation_token_v1_set_serial(token, old_serial, wl_seat_g);
    xdg_activation_token_v1_set_surface(token, first.surface);
    xdg_activation_token_v1_commit(token);
    while (!token_done && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!token_done) return 1;

    xdg_activation_v1_activate(activation, token_value, first.surface);
    if (wl_display_roundtrip(wl_dpy) < 0 ||
        wl_display_roundtrip(wl_dpy) < 0) return 1;
    printf("activation replay attempted\n");
    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
