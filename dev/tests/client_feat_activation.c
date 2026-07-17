// Feature: xdg-activation. With A focused and B in the background, B requests
// an activation token (off a real input serial) and activates itself — the
// keyboard focus must move to B.

#include "wl_util.h"
#include <xdg-activation-v1-client-protocol.h>

static struct xdg_activation_v1* activation;
static struct wl_toplevel_ctx a, b;
static char token[256];
static int got_token;

static void token_done(void* d, struct xdg_activation_token_v1* t, const char* tok) {
    (void)d; (void)t;
    snprintf(token, sizeof(token), "%s", tok);
    got_token = 1;
}
static const struct xdg_activation_token_v1_listener token_listener = {token_done};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_activation_v1_interface.name))
        activation = wl_registry_bind(r, name, &xdg_activation_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_seat_g || !wl_kbd) { fprintf(stderr, "no seat/keyboard\n"); return 1; }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!activation) { fprintf(stderr, "no xdg-activation\n"); return 1; }

    // map B first, then A — A ends up focused
    wl_make_toplevel(&b, "client_feat_activation_B", 300, 200, 0xFF0000FF);
    wl_make_toplevel(&a, "client_feat_activation_A", 300, 200, 0xFFFF0000);
    printf("client_feat_activation: mapped\n");

    // wait until A is focused and we have a serial (the scenario injects a key)
    for (int i = 0; i < 200 && !(wlk_focus == a.surface && wlk_key_serial); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (wlk_focus != a.surface || !wlk_key_serial) { fprintf(stderr, "A not focused / no serial\n"); return 1; }

    // B requests an activation token and activates itself
    struct xdg_activation_token_v1* tok = xdg_activation_v1_get_activation_token(activation);
    xdg_activation_token_v1_add_listener(tok, &token_listener, NULL);
    xdg_activation_token_v1_set_serial(tok, wlk_key_serial, wl_seat_g);
    xdg_activation_token_v1_set_surface(tok, b.surface);
    xdg_activation_token_v1_commit(tok);

    for (int i = 0; i < 100 && !got_token; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!got_token) { fprintf(stderr, "no activation token\n"); return 1; }
    printf("client_feat_activation: token %s\n", token);

    xdg_activation_v1_activate(activation, token, b.surface);

    for (int i = 0; i < 100 && wlk_focus != b.surface; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (wlk_focus != b.surface) { fprintf(stderr, "focus did not move to B\n"); return 1; }

    printf("client_feat_activation: focus moved to B\n");
    printf("client_feat_activation: ok\n");
    return 0;
}
