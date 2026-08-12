// Feature: xdg_popup.reposition (v3). A popup placed at the parent's
// top-left corner moves to (110,110) after a reposition with a new
// positioner; the compositor must send repositioned(token) followed by the
// new configure. KEY_1 triggers the reposition.

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_toplevel_ctx top;
static struct wl_surface* popup_surface;
static struct xdg_surface* popup_xs;
static struct xdg_popup* popup;
static int popup_committed;
static uint32_t seen_token;
static int last_x, last_y;

static void popup_xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!popup_committed) {
        wl_surface_attach(popup_surface, wl_solid(80, 60, 0xFFFFFF00), 0, 0);
        wl_surface_damage(popup_surface, 0, 0, 80, 60);
        wl_surface_commit(popup_surface);
        popup_committed = 1;
        printf("popup mapped\n");
    } else {
        // apply the repositioned placement
        wl_surface_commit(popup_surface);
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d; (void)p; (void)w; (void)h;
    last_x = x;
    last_y = y;
    printf("popup configure %d,%d\n", x, y);
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; printf("popup done\n"); }
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p;
    seen_token = token;
    printf("repositioned token=%u\n", token);
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done,
                                                         popup_repositioned};

static struct xdg_positioner* make_pos(int ax, int ay) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 80, 60);
    xdg_positioner_set_anchor_rect(pos, ax, ay, 10, 10);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    return pos;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    wl_make_toplevel(&top, "reposition", 300, 200, 0xFFFF0000);

    struct xdg_positioner* pos = make_pos(0, 0);
    popup_surface = wl_compositor_create_surface(wl_comp);
    popup_xs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
    xdg_surface_add_listener(popup_xs, &popup_xs_listener, NULL);
    popup = xdg_surface_get_popup(popup_xs, top.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    wl_surface_commit(popup_surface);
    xdg_positioner_destroy(pos);

    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct xdg_positioner* pos2 = make_pos(100, 100);
    xdg_popup_reposition(popup, pos2, 7);
    wl_display_flush(wl_dpy);
    xdg_positioner_destroy(pos2);

    while ((seen_token != 7 || last_x != 110) && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("reposition ok\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
