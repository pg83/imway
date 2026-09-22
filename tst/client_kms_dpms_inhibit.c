// Idle inhibitors against display power management. One inhibitor sits on
// a surface with no content (it holds nothing off), one on a mapped popup
// (a popup's root is no toplevel, yet a shown one keeps the display on).
// The scenario touches go-release to drop the popup's inhibitor.

#include "wl_util.h"
#include <idle-inhibit-unstable-v1-client-protocol.h>

static struct zwp_idle_inhibit_manager_v1* inhibit_mgr;

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_idle_inhibit_manager_v1_interface.name))
        inhibit_mgr = wl_registry_bind(r, name, &zwp_idle_inhibit_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

static int popup_committed;

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct wl_surface* surface = d;

    xdg_surface_ack_configure(xs, serial);

    if (!popup_committed) {
        wl_surface_attach(surface, wl_solid(60, 40, 0xff00ffffu), 0, 0);
        wl_surface_commit(surface);
        popup_committed = 1;
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) return 2;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!inhibit_mgr) return 2;

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "dpms-inhibit", 200, 150, 0xFFFF0000);

    zwp_idle_inhibit_manager_v1_create_inhibitor(inhibit_mgr, wl_compositor_create_surface(wl_comp));

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

    xdg_positioner_set_size(pos, 60, 40);
    xdg_positioner_set_anchor_rect(pos, 10, 10, 20, 20);

    struct wl_surface* psurf = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wl_wm, psurf);

    xdg_surface_add_listener(pxs, &popup_xs_listener, psurf);

    struct xdg_popup* popup = xdg_surface_get_popup(pxs, top.xs, pos);

    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_positioner_destroy(pos);
    wl_surface_commit(psurf);

    while (!popup_committed && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct zwp_idle_inhibitor_v1* held = zwp_idle_inhibit_manager_v1_create_inhibitor(inhibit_mgr, psurf);

    wl_display_roundtrip(wl_dpy);
    printf("inhibited\n");

    while (access("go-release", F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }

    zwp_idle_inhibitor_v1_destroy(held);
    wl_display_roundtrip(wl_dpy);
    printf("released\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
