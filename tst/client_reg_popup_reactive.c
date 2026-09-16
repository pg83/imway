/* A reactive popup follows the work area. The parent is an ordinary window
 * in the lower right; the popup is anchored to its bottom edge with slide,
 * so it is constrained. When the scenario moves the dock to the bottom the
 * work area shrinks and the compositor must configure the popup again. */

#include "wl_util.h"

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int cur_w = 400, cur_h = 300;
static int committed;

static struct wl_surface* popup_surface;
static struct xdg_surface* popup_xs;
static struct xdg_popup* popup;
static int popup_committed;
static int configures;
static int last_y = -1;
static int moved;

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t; (void)w; (void)h; (void)states;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!committed) {
        wl_surface_attach(surface, wl_solid(cur_w, cur_h, 0xFF3060C0), 0, 0);
        wl_surface_damage(surface, 0, 0, cur_w, cur_h);
        wl_surface_commit(surface);
        committed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void popup_xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!popup_committed) {
        wl_surface_attach(popup_surface, wl_solid(200, 150, 0xFFFFFF00), 0, 0);
        wl_surface_damage(popup_surface, 0, 0, 200, 150);
        wl_surface_commit(popup_surface);
        popup_committed = 1;
        printf("popup mapped\n");
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d; (void)p; (void)w; (void)h;
    configures++;
    if (last_y >= 0 && y != last_y) {
        moved++;
        printf("popup moved %d -> %d\n", last_y, y);
    }
    last_y = y;
    printf("popup configure %d,%d (%d)\n", x, y, configures);
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done,
                                                         popup_repositioned};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "popup-reactive");
    xdg_toplevel_set_app_id(tl, "popup-reactive");
    wl_surface_commit(surface);

    while (!committed && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

    /* anchored below the parent's bottom edge, sliding to stay on screen */
    xdg_positioner_set_size(pos, 200, 150);
    xdg_positioner_set_anchor_rect(pos, 0, cur_h - 10, cur_w, 10);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM);
    xdg_positioner_set_constraint_adjustment(
        pos, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
    xdg_positioner_set_reactive(pos);

    popup_surface = wl_compositor_create_surface(wl_comp);
    popup_xs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
    xdg_surface_add_listener(popup_xs, &popup_xs_listener, NULL);
    popup = xdg_surface_get_popup(popup_xs, xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    wl_surface_commit(popup_surface);
    xdg_positioner_destroy(pos);

    while (!popup_committed && wl_display_dispatch(wl_dpy) != -1) {
    }

    /* the scenario now shrinks the work area from below */
    for (int i = 0; i < 600 && !moved; i++) {
        if (wl_display_dispatch_pending(wl_dpy) < 0 || wl_display_flush(wl_dpy) < 0) break;
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }

    if (!moved) {
        fprintf(stderr, "the reactive popup never followed the work area\n");
        return 1;
    }

    printf("reactive popup followed\n");

    return 0;
}
