// The pointer over a popup enters the popup's surface, not the window under
// it, while a popup and a toplevel that never got content sit in the scene
// lists unpicked. The scenario reads where the popup landed from the dump.

#include "wl_util.h"

static int popup_committed;
static struct wl_surface* popup_surface;

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);

    if (!popup_committed) {
        wl_surface_attach(popup_surface, wl_solid(100, 80, 0xff20c020u), 0, 0);
        wl_surface_commit(popup_surface);
        popup_committed = 1;
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void empty_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
}
static const struct xdg_surface_listener empty_xs_listener = {empty_xs_configure};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static struct xdg_popup* make_popup(struct xdg_surface* parent, struct wl_surface* surface,
                                    const struct xdg_surface_listener* listener) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

    xdg_positioner_set_size(pos, 100, 80);
    xdg_positioner_set_anchor_rect(pos, 40, 40, 1, 1);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);

    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

    xdg_surface_add_listener(xs, listener, NULL);

    struct xdg_popup* popup = xdg_surface_get_popup(xs, parent, pos);

    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_positioner_destroy(pos);
    wl_surface_commit(surface);

    return popup;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    if (wl_boot() || !wl_ptr) return 2;

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "pointer-popup", 300, 240, 0xffc02020u);

    // a toplevel role with no content ever
    struct wl_surface* bare = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* bare_xs = xdg_wm_base_get_xdg_surface(wl_wm, bare);

    xdg_surface_add_listener(bare_xs, &empty_xs_listener, NULL);
    xdg_toplevel_set_app_id(xdg_surface_get_toplevel(bare_xs), "pointer-popup-bare");
    wl_surface_commit(bare);

    // the real popup first, then one that never gets a buffer: it is the
    // newest in the popup list and has to be passed over
    popup_surface = wl_compositor_create_surface(wl_comp);
    make_popup(top.xs, popup_surface, &popup_xs_listener);

    while (!popup_committed && wl_display_dispatch(wl_dpy) != -1) {
    }

    make_popup(top.xs, wl_compositor_create_surface(wl_comp), &empty_xs_listener);
    wl_display_roundtrip(wl_dpy);
    printf("popup mapped\n");

    while (wlp_focus != popup_surface && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("pointer on the popup\n");

    return 0;
}
