#include "wl_util.h"

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int mapped;

static void xs_configure(void* data, struct xdg_surface* xdg, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(xdg, serial);
    if (!mapped) {
        wl_surface_attach(surface, wl_solid(300, 200, 0xffff0000), 0, 0);
        wl_surface_damage(surface, 0, 0, 300, 200);
        wl_surface_commit(surface);
        mapped = 1;
        puts("dock client ready");
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* data, struct xdg_toplevel* top, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)data; (void)top; (void)w; (void)h; (void)states;
}
static void tl_close(void* data, struct xdg_toplevel* top) { (void)data; (void)top; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_app_id(tl, "dock-test");
    xdg_toplevel_set_title(tl, "dock-test");
    wl_surface_commit(surface);

    while (!mapped && wl_display_dispatch(wl_dpy) >= 0) {
    }

    for (int i = 0; i < 50; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 2;
        usleep(20000);
    }

    xdg_toplevel_set_minimized(tl);
    wl_display_flush(wl_dpy);
    puts("minimize requested");

    while (wl_display_dispatch(wl_dpy) >= 0) {
    }
    return 0;
}
