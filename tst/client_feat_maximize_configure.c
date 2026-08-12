// Feature: xdg_toplevel.set_maximized/unset_maximized must each be answered
// with a configure even if the compositor does not maximize (the spec
// requires a reply so the client is not left waiting; the state set may
// stay unchanged).

#include "wl_util.h"

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int cfgs, committed, maximized;

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    cfgs++;
    if (!committed) {
        wl_surface_attach(surface, wl_solid(300, 200, 0xFFFF0000), 0, 0);
        wl_surface_damage(surface, 0, 0, 300, 200);
        wl_surface_commit(surface);
        committed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* s) {
    (void)d; (void)t;
    maximized = 0;
    uint32_t* state;
    wl_array_for_each(state, s) {
        if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED) maximized = 1;
    }
    if (maximized && (w < 1000 || h < 600)) {
        fprintf(stderr, "maximized configure is too small: %dx%d\n", w, h);
        exit(2);
    }
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static int await_cfgs(int want) {
    for (int i = 0; i < 100 && cfgs < want; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 0;
        usleep(20000);
    }
    return cfgs >= want;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_app_id(tl, "maximize");
    wl_surface_commit(surface);
    while (!committed && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);
    int base = cfgs;

    xdg_toplevel_set_maximized(tl);
    wl_display_flush(wl_dpy);
    if (!await_cfgs(base + 1)) {
        fprintf(stderr, "no configure in reply to set_maximized\n");
        return 1;
    }
    if (!maximized) {
        fprintf(stderr, "set_maximized reply omitted MAXIMIZED state\n");
        return 1;
    }
    printf("set_maximized answered\n");

    xdg_toplevel_unset_maximized(tl);
    wl_display_flush(wl_dpy);
    if (!await_cfgs(base + 2)) {
        fprintf(stderr, "no configure in reply to unset_maximized\n");
        return 1;
    }
    if (maximized) {
        fprintf(stderr, "unset_maximized reply retained MAXIMIZED state\n");
        return 1;
    }
    printf("unset_maximized answered\n");
    return 0;
}
