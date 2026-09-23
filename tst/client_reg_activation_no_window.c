// xdg-activation tokens authorized by a real key press, spent on a surface
// that is no window (a bare wl_surface), on a toplevel that never mapped,
// and on the mapped window. Only the last activates anything; the scenario
// reads the compositor's activation log for the three tokens printed here.

#include "wl_util.h"
#include <xdg-activation-v1-client-protocol.h>

static struct xdg_activation_v1* activation;

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_activation_v1_interface.name))
        activation = wl_registry_bind(r, name, &xdg_activation_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

static char token_str[256];
static int have_token;

static void token_done(void* d, struct xdg_activation_token_v1* t, const char* token) {
    (void)d; (void)t;
    snprintf(token_str, sizeof token_str, "%s", token);
    have_token = 1;
}
static const struct xdg_activation_token_v1_listener token_listener = {token_done};

static void spend(const char* what, uint32_t serial, struct wl_surface* target) {
    struct xdg_activation_token_v1* tok = xdg_activation_v1_get_activation_token(activation);

    have_token = 0;
    xdg_activation_token_v1_add_listener(tok, &token_listener, NULL);
    xdg_activation_token_v1_set_serial(tok, serial, wl_seat_g);
    xdg_activation_token_v1_commit(tok);
    while (!have_token && wl_display_dispatch(wl_dpy) != -1) {
    }
    xdg_activation_v1_activate(activation, token_str, target);
    wl_display_roundtrip(wl_dpy);
    xdg_activation_token_v1_destroy(tok);
    printf("token %s %s\n", what, token_str);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot() || !wl_kbd) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!activation) {
        fprintf(stderr, "no xdg-activation\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "activation-no-window", 240, 160, 0xFFFF0000);
    printf("ready\n");
    while (!wlk_press_serial && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wl_surface* bare = wl_compositor_create_surface(wl_comp);
    struct wl_surface* pending = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* pending_xs = xdg_wm_base_get_xdg_surface(wl_wm, pending);
    struct xdg_toplevel* pending_tl = xdg_surface_get_toplevel(pending_xs);

    xdg_toplevel_set_title(pending_tl, "never-mapped");
    spend("bare", wlk_press_serial, bare);
    spend("unmapped", wlk_press_serial, pending);
    spend("mapped", wlk_press_serial, top.surface);
    printf("spent\n");
    return 0;
}
