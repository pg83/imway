// Feature: xdg_toplevel configure states. On map the focused toplevel must be
// told it is ACTIVATED; set_fullscreen must yield a configure carrying the
// FULLSCREEN state and the full output size.

#include "wl_util.h"

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int mapped, activated_seen, fullscreen_seen, cfg_w, cfg_h;

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t;
    cfg_w = w; cfg_h = h;
    uint32_t* s;
    int act = 0, fs = 0;
    wl_array_for_each(s, states) {
        if (*s == XDG_TOPLEVEL_STATE_ACTIVATED) act = 1;
        if (*s == XDG_TOPLEVEL_STATE_FULLSCREEN) fs = 1;
    }
    if (act) activated_seen = 1;
    if (fs) fullscreen_seen = 1;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void xs_configure(void* d, struct xdg_surface* x, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(x, serial);
    if (!mapped) {
        wl_surface_attach(surface, wl_solid(400, 300, 0xFFFF0000), 0, 0);
        wl_surface_commit(surface);
        mapped = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "client_feat_xdg_states");
    wl_surface_commit(surface);

    // wait for the mapped + focused (ACTIVATED) configure
    for (int i = 0; i < 200 && !(mapped && activated_seen); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!activated_seen) { fprintf(stderr, "no ACTIVATED state on focus\n"); return 1; }
    printf("client_feat_xdg_states: activated\n");

    xdg_toplevel_set_fullscreen(tl, NULL);
    wl_surface_commit(surface);

    for (int i = 0; i < 200 && !fullscreen_seen; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!fullscreen_seen) { fprintf(stderr, "no FULLSCREEN state\n"); return 1; }

    printf("client_feat_xdg_states: fullscreen %dx%d\n", cfg_w, cfg_h);
    if (cfg_w < 1000 || cfg_h < 600) { fprintf(stderr, "fullscreen did not size to output\n"); return 1; }

    printf("client_feat_xdg_states: ok\n");
    return 0;
}
