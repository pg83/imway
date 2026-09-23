// set_maximized and unset_maximized both sent before the window first maps:
// the window maps at its own size, not maximized, with no restore of a size
// it never had.

#include "wl_util.h"

static struct wl_surface* surface;
static int configured, committed;
static int32_t cfg_w, cfg_h;
static int cfg_maximized;

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    configured = 1;
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* states) {
    (void)d; (void)t;
    uint32_t* s;
    cfg_w = w;
    cfg_h = h;
    cfg_maximized = 0;
    wl_array_for_each(s, states) {
        if (*s == XDG_TOPLEVEL_STATE_MAXIMIZED) cfg_maximized = 1;
    }
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; }
static void tl_bounds(void* d, struct xdg_toplevel* t, int32_t w, int32_t h) { (void)d; (void)t; (void)w; (void)h; }
static void tl_caps(void* d, struct xdg_toplevel* t, struct wl_array* c) { (void)d; (void)t; (void)c; }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close, tl_bounds, tl_caps};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 2;

    surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "maximize-undone");
    xdg_toplevel_set_app_id(tl, "maximize-undone");
    xdg_toplevel_set_maximized(tl);
    xdg_toplevel_unset_maximized(tl);
    wl_surface_commit(surface);
    while (!configured && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (cfg_maximized) {
        fprintf(stderr, "the first configure was maximized\n");
        return 1;
    }
    wl_surface_attach(surface, wl_solid(220, 140, 0xFF40A0A0), 0, 0);
    wl_surface_damage(surface, 0, 0, 220, 140);
    wl_surface_commit(surface);
    committed = 1;
    wl_display_roundtrip(wl_dpy);
    printf("mapped\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/go-exit", getenv("XDG_RUNTIME_DIR"));
    while (access(path, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }
    return 0;
}
