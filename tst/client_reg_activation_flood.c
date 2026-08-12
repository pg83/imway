/* PLAN limits #4: flood the activation token queue (cap 64). Old tokens fall
 * out; activating with an evicted or a fresh token must both be safe. */
#include "wl_util.h"

#include <xdg-activation-v1-client-protocol.h>

static struct xdg_activation_v1* activation;

static void act_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                       uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, xdg_activation_v1_interface.name))
        activation = wl_registry_bind(r, name, &xdg_activation_v1_interface, 1);
}
static void act_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener act_listener = {act_global, act_remove};

static char token_buf[2][256];
static int token_done;

static void token_event(void* d, struct xdg_activation_token_v1* t, const char* token) {
    (void)t;
    char* dst = d;
    if (dst) snprintf(dst, 256, "%s", token);
    token_done = 1;
}
static const struct xdg_activation_token_v1_listener token_listener = {token_event};

int main(void) {
    alarm(30);
    if (wl_boot()) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &act_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!activation) return 77;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "activation-flood", 64, 64, 0xffff0000);

    for (int i = 0; i < 128; i++) {
        struct xdg_activation_token_v1* t = xdg_activation_v1_get_activation_token(activation);
        char* dst = i == 0 ? token_buf[0] : i == 127 ? token_buf[1] : NULL;
        xdg_activation_token_v1_add_listener(t, &token_listener, dst);
        xdg_activation_token_v1_set_surface(t, top.surface);
        xdg_activation_token_v1_commit(t);
        token_done = 0;
        while (!token_done && wl_display_dispatch(wl_dpy) != -1) {
        }
        xdg_activation_token_v1_destroy(t);
    }

    /* evicted token: must be silently useless; fresh token: must be safe */
    xdg_activation_v1_activate(activation, token_buf[0], top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    xdg_activation_v1_activate(activation, token_buf[1], top.surface);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
