// Positioner constraint adjustment. A fullscreen toplevel parents a popup
// whose anchor is the bottom-right corner of the screen with gravity
// bottom-right, so the popup is constrained on both axes. Mode (argv[1])
// picks the adjustment: "slide" clamps it into the screen, "flip" mirrors it
// to the other side of the anchor. The scenario asserts the placed position
// via the control dump. Runs until killed.

#include "wl_util.h"

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static struct wl_surface* popup_surface;
static struct xdg_surface* popup_xs;
static struct xdg_popup* popup;
static int cur_w, cur_h;
static int pend_w, pend_h;
static int fullscreen_seen, committed;
static int popup_committed;

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t;
    uint32_t* s;
    wl_array_for_each(s, states) {
        if (*s == XDG_TOPLEVEL_STATE_FULLSCREEN) fullscreen_seen = 1;
    }
    pend_w = w;
    pend_h = h;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (pend_w > 0 && pend_h > 0 && (pend_w != cur_w || pend_h != cur_h || !committed)) {
        cur_w = pend_w;
        cur_h = pend_h;
        wl_surface_attach(surface, wl_solid(cur_w, cur_h, 0xFF404040), 0, 0);
        wl_surface_damage(surface, 0, 0, cur_w, cur_h);
        wl_surface_commit(surface);
        committed = 1;
        printf("fullscreen %dx%d\n", cur_w, cur_h);
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
    (void)d; (void)p;
    printf("popup configure %d,%d %dx%d\n", x, y, w, h);
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)d; (void)p;
    printf("popup done\n");
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done,
                                                         popup_repositioned};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char* mode = argc > 1 ? argv[1] : "slide";
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "positioner");
    xdg_toplevel_set_app_id(tl, "positioner");
    xdg_toplevel_set_fullscreen(tl, NULL);
    wl_surface_commit(surface);

    while ((!committed || !fullscreen_seen) && wl_display_dispatch(wl_dpy) != -1) {
    }

    // anchor rect: the 10x10 bottom-right corner of the parent; anchoring
    // bottom-right with gravity bottom-right pushes the popup off screen on
    // both axes unless the adjustment kicks in
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 200, 150);
    xdg_positioner_set_anchor_rect(pos, cur_w - 10, cur_h - 10, 10, 10);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    uint32_t adj = !strcmp(mode, "flip")
        ? (XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y)
        : (XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
    xdg_positioner_set_constraint_adjustment(pos, adj);

    popup_surface = wl_compositor_create_surface(wl_comp);
    popup_xs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
    xdg_surface_add_listener(popup_xs, &popup_xs_listener, NULL);
    popup = xdg_surface_get_popup(popup_xs, xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    wl_surface_commit(popup_surface);
    xdg_positioner_destroy(pos);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
