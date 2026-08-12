// xdg-shell v7: the compositor must advertise xdg_wm_base at version >= 7 and,
// on a toplevel, send wm_capabilities (v5) and configure_bounds (v4) with the
// work area before the first configure.

#define REG_XDG_VERSION 7
#include "wl_util.h"

static struct wl_surface* surface;
static int got_bounds, bounds_w, bounds_h;
static int got_caps, cap_maximize, cap_fullscreen, cap_minimize;
static int configured, committed;

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t; (void)w; (void)h; (void)states;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static void tl_configure_bounds(void* d, struct xdg_toplevel* t, int32_t w, int32_t h) {
    (void)d; (void)t;
    got_bounds = 1; bounds_w = w; bounds_h = h;
}
static void tl_wm_capabilities(void* d, struct xdg_toplevel* t, struct wl_array* caps) {
    (void)d; (void)t;
    got_caps = 1;
    uint32_t* c;
    for (c = (uint32_t*)caps->data;
         (const char*)c < (const char*)caps->data + caps->size; c++) {
        if (*c == XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE) cap_maximize = 1;
        else if (*c == XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN) cap_fullscreen = 1;
        else if (*c == XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE) cap_minimize = 1;
    }
}
static const struct xdg_toplevel_listener tl_listener = {
    .configure = tl_configure,
    .close = tl_close,
    .configure_bounds = tl_configure_bounds,
    .wm_capabilities = tl_wm_capabilities,
};

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    configured = 1;
    if (!committed) {
        wl_surface_attach(surface, wl_solid(200, 150, 0xFF3080FFu), 0, 0);
        wl_surface_damage(surface, 0, 0, 200, 150);
        wl_surface_commit(surface);
        committed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    if (wl_wm && xdg_wm_base_get_version(wl_wm) < 7) {
        fprintf(stderr, "xdg_wm_base advertised at version %u, want >= 7\n",
                xdg_wm_base_get_version(wl_wm));
        return 1;
    }

    surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "xdg-v7");
    xdg_toplevel_set_app_id(tl, "xdg-v7");
    wl_surface_commit(surface);

    while (!configured && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (!got_caps) {
        fprintf(stderr, "no wm_capabilities before first configure\n");
        return 1;
    }
    if (!cap_maximize || !cap_fullscreen || !cap_minimize) {
        fprintf(stderr, "wm_capabilities missing expected caps (max=%d fs=%d min=%d)\n",
                cap_maximize, cap_fullscreen, cap_minimize);
        return 1;
    }
    if (!got_bounds) {
        fprintf(stderr, "no configure_bounds before first configure\n");
        return 1;
    }
    if (bounds_w <= 0 || bounds_h <= 0) {
        fprintf(stderr, "configure_bounds has non-positive size %dx%d\n", bounds_w, bounds_h);
        return 1;
    }

    printf("client_reg_xdg_version_7: caps ok, bounds %dx%d\n", bounds_w, bounds_h);
    return 0;
}
