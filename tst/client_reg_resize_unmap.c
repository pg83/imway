// Regression: unmap in the middle of an interactive border resize. The
// SSD window answers configures with matching buffers; KEY_U null-attaches
// it while the border drag is held, KEY_R remaps it. The compositor must
// survive the window vanishing under an active resize and resize the
// remapped window again.

#include "wl_util.h"
#include <linux/input-event-codes.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>

static struct zxdg_decoration_manager_v1* deco_mgr;
static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int cur_w = 300, cur_h = 200;
static int pend_w, pend_h;
static int mapped_printed, unmapped;

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t; (void)states;
    pend_w = w;
    pend_h = h;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (unmapped) return; // an unmapped window answers nothing
    if (pend_w > 0) cur_w = pend_w;
    if (pend_h > 0) cur_h = pend_h;
    wl_surface_attach(surface, wl_solid(cur_w, cur_h, 0xFFFF0000), 0, 0);
    wl_surface_damage(surface, 0, 0, cur_w, cur_h);
    wl_surface_commit(surface);
    if (!mapped_printed) {
        printf("resize client mapped\n");
        mapped_printed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!deco_mgr) { fprintf(stderr, "no xdg-decoration manager\n"); return 1; }

    // keeps keyboard focus alive while the main window is unmapped, so the
    // KEY_R remap trigger still reaches this client
    struct wl_toplevel_ctx helper;
    wl_make_toplevel(&helper, "helper", 60, 60, 0xFF00FF00);

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "rsunmap");
    xdg_toplevel_set_app_id(tl, "rsunmap");

    struct zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, tl);
    zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    wl_surface_commit(surface);

    wlk_watch_key = KEY_U;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    unmapped = 1;
    wl_surface_attach(surface, NULL, 0, 0);
    wl_surface_commit(surface);
    wl_display_roundtrip(wl_dpy);
    printf("unmapped\n");

    wlk_watch_key = KEY_R;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    unmapped = 0;
    mapped_printed = 0;
    cur_w = 300;
    cur_h = 200;
    pend_w = pend_h = 0;
    wl_surface_commit(surface); // fresh initial commit
    while (!mapped_printed && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);
    printf("remapped\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
