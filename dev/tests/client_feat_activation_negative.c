// Feature: xdg-activation must not allow focus stealing. Mode "victim" maps
// a red toplevel and idles. Mode "thief" maps a blue one, waits until it
// LOSES the activated state (the scenario focuses the victim), then requests
// a token with no input serial and activates itself — the unauthorized token
// must not move focus.

#include "wl_util.h"
#include <xdg-activation-v1-client-protocol.h>

static struct xdg_activation_v1* activation;
static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int committed, was_activated, deactivated;
static char token_str[256];
static int have_token;

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!committed) {
        wl_surface_attach(surface, wl_solid(200, 150, 0xFF0000FF), 0, 0);
        wl_surface_damage(surface, 0, 0, 200, 150);
        wl_surface_commit(surface);
        committed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t; (void)w; (void)h;
    int act = 0;
    uint32_t* s;
    wl_array_for_each(s, states) {
        if (*s == XDG_TOPLEVEL_STATE_ACTIVATED) act = 1;
    }
    if (act) was_activated = 1;
    if (was_activated && !act) deactivated = 1;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void token_done(void* d, struct xdg_activation_token_v1* t, const char* token) {
    (void)d; (void)t;
    snprintf(token_str, sizeof token_str, "%s", token);
    have_token = 1;
}
static const struct xdg_activation_token_v1_listener token_listener = {token_done};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_activation_v1_interface.name))
        activation = wl_registry_bind(r, name, &xdg_activation_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char* mode = argc > 1 ? argv[1] : "victim";
    alarm(30);
    if (wl_boot()) return 1;

    if (!strcmp(mode, "victim")) {
        struct wl_toplevel_ctx top;
        wl_make_toplevel(&top, "victim", 300, 200, 0xFFFF0000);
        printf("victim ready\n");
        while (wl_display_dispatch(wl_dpy) != -1) {
        }
        return 0;
    }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!activation) { fprintf(stderr, "no xdg-activation\n"); return 1; }

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "thief");
    xdg_toplevel_set_app_id(tl, "thief");
    wl_surface_commit(surface);
    while (!committed && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("thief ready\n");

    // wait until the scenario moves focus to the victim
    while (!deactivated && wl_display_dispatch(wl_dpy) != -1) {
    }

    // no set_serial: the token must come back unauthorized
    struct xdg_activation_token_v1* tok = xdg_activation_v1_get_activation_token(activation);
    xdg_activation_token_v1_add_listener(tok, &token_listener, NULL);
    xdg_activation_token_v1_set_app_id(tok, "thief");
    xdg_activation_token_v1_set_surface(tok, surface);
    xdg_activation_token_v1_commit(tok);
    while (!have_token && wl_display_dispatch(wl_dpy) != -1) {
    }
    xdg_activation_v1_activate(activation, token_str, surface);
    wl_display_roundtrip(wl_dpy);
    printf("stole attempt\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
