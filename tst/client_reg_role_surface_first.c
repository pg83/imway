// wl_surfaces destroyed before their xdg roles, while mapped: a popup's
// surface goes while the popup and its window stay, frames are drawn with
// the popup left without one, and the popup's objects go after; then a
// window's surface goes the same way while another window is shown. The
// roles are inert without their surfaces, and destroying them later is
// no error.

#include "wl_util.h"

static int popup_configured;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)d; (void)p;
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    popup_configured = 1;
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top, other;

    wl_make_toplevel(&top, "role-surface-first", 240, 160, 0xffff0000);

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 80, 60);
    xdg_positioner_set_anchor_rect(pos, 10, 10, 20, 20);

    struct wl_surface* ps = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wl_wm, ps);
    xdg_surface_add_listener(pxs, &popup_xs_listener, NULL);
    struct xdg_popup* popup = xdg_surface_get_popup(pxs, top.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_positioner_destroy(pos);
    wl_surface_commit(ps);
    while (!popup_configured && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_surface_attach(ps, wl_solid(80, 60, 0xff00ff00), 0, 0);
    wl_surface_damage(ps, 0, 0, 80, 60);
    wl_surface_commit(ps);
    wl_await_presented(ps);

    // the popup's surface first; the window keeps drawing
    wl_surface_destroy(ps);
    wl_await_presented(top.surface);
    wl_await_presented(top.surface);
    xdg_popup_destroy(popup);
    xdg_surface_destroy(pxs);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "the popup's objects could not go after its surface\n");
        return 1;
    }
    printf("popup surface went first\n");

    // then the window's surface, with another window on screen
    wl_make_toplevel(&other, "role-surface-other", 120, 90, 0xff0000ff);
    wl_surface_destroy(top.surface);
    wl_await_presented(other.surface);
    wl_await_presented(other.surface);
    xdg_toplevel_destroy(top.tl);
    xdg_surface_destroy(top.xs);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "the window's roles could not go after its surface\n");
        return 1;
    }
    printf("window surface went first\n");

    return 0;
}
