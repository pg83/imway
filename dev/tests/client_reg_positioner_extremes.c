/* PLAN arithmetic #8: positioner anchor rect and offset near INT32_MIN/MAX
 * with every constraint adjustment enabled. The popup must still get a
 * clamped configure; nothing overflows. */
#include "wl_util.h"

#include <limits.h>

static int popup_configured;
static int popup_x, popup_y, popup_w, popup_h;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d; (void)p;
    popup_configured = 1;
    popup_x = x;
    popup_y = y;
    popup_w = w;
    popup_h = h;
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done,
                                                         popup_repositioned};

static int popup_committed;

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct wl_surface* surface = d;
    xdg_surface_ack_configure(xs, serial);
    if (!popup_committed) {
        wl_surface_attach(surface, wl_solid(20, 20, 0xff00ffff), 0, 0);
        wl_surface_commit(surface);
        popup_committed = 1;
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "positioner-extremes", 128, 128, 0xffff0000);

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 20, 20);
    xdg_positioner_set_anchor_rect(pos, INT32_MAX - 8, INT32_MAX - 8, 8, 8);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_offset(pos, INT32_MAX, INT32_MAX);
    xdg_positioner_set_constraint_adjustment(
        pos, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y |
                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X |
                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y);

    struct wl_surface* psurf = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wl_wm, psurf);
    xdg_surface_add_listener(pxs, &popup_xs_listener, psurf);
    struct xdg_popup* popup = xdg_surface_get_popup(pxs, top.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_positioner_destroy(pos);
    wl_surface_commit(psurf);

    while ((!popup_configured || !popup_committed) && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!popup_configured) return 1;
    if (popup_w <= 0 || popup_h <= 0) {
        fprintf(stderr, "degenerate popup size %dx%d\n", popup_w, popup_h);
        return 1;
    }

    /* the mirror case: everything at the negative extreme */
    struct xdg_positioner* neg = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(neg, 20, 20);
    xdg_positioner_set_anchor_rect(neg, INT32_MIN, INT32_MIN, 8, 8);
    xdg_positioner_set_anchor(neg, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(neg, XDG_POSITIONER_GRAVITY_TOP_LEFT);
    xdg_positioner_set_offset(neg, INT32_MIN, INT32_MIN);
    xdg_positioner_set_constraint_adjustment(
        neg, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X);
    xdg_popup_reposition(popup, neg, 1);
    xdg_positioner_destroy(neg);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    xdg_popup_destroy(popup);
    xdg_surface_destroy(pxs);
    wl_surface_destroy(psurf);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
