// Two grab popups of the same toplevel, stacked in opening order. The client
// destroys the first popup's wl_surface while the second still tops the grab
// stack, then exits. The buried surface's death must unlink it from the stack
// without moving the keyboard off the top popup, and the exit must empty the
// stack so the keyboard reaches the next client.

#include "wl_util.h"

static struct wl_toplevel_ctx top;

struct pop {
    struct wl_surface* surface;
    struct xdg_surface* xs;
    struct xdg_popup* popup;
    int committed;
};
static struct pop p1, p2;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_reposition(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_reposition};

static void popup_xdg_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct pop* pp = d;
    xdg_surface_ack_configure(xs, serial);
    if (!pp->committed) {
        wl_surface_attach(pp->surface, wl_solid(120, 90, 0xFFFFFF00), 0, 0);
        wl_surface_commit(pp->surface);
        pp->committed = 1;
    }
}
static const struct xdg_surface_listener popup_xdg_listener = {popup_xdg_configure};

static void open_popup(struct pop* pp, struct xdg_surface* parent, uint32_t serial) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 120, 90);
    xdg_positioner_set_anchor_rect(pos, 10, 10, 40, 20);
    pp->surface = wl_compositor_create_surface(wl_comp);
    pp->xs = xdg_wm_base_get_xdg_surface(wl_wm, pp->surface);
    xdg_surface_add_listener(pp->xs, &popup_xdg_listener, pp);
    pp->popup = xdg_surface_get_popup(pp->xs, parent, pos);
    xdg_popup_add_listener(pp->popup, &popup_listener, NULL);
    xdg_popup_grab(pp->popup, wl_seat_g, serial);
    xdg_positioner_destroy(pos);
    wl_surface_commit(pp->surface);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot() || !wl_kbd || !wl_ptr) {
        fprintf(stderr, "sibling-grab-exit: no keyboard/pointer\n");
        return 1;
    }

    wl_make_toplevel(&top, "sibling-grab-exit", 400, 300, 0xFFFF0000);
    printf("sibling-grab-exit mapped\n");

    while (!(wlp_button_count > 0 && wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED) && wl_display_dispatch(wl_dpy) != -1) {
    }

    uint32_t serial = wlp_button_serial;

    open_popup(&p1, top.xs, serial);
    while (!(p1.committed && wlk_focus == p1.surface) && wl_display_dispatch(wl_dpy) != -1) {
    }

    open_popup(&p2, top.xs, serial);
    while (!(p2.committed && wlk_focus == p2.surface) && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("sibling-grab-exit both grabbed\n");

    // the buried popup's wl_surface goes, its role object still alive: the
    // second popup tops the grab stack and keeps the keyboard
    wl_surface_destroy(p1.surface);
    p1.surface = NULL;
    int leaves = wlk_leaves;
    if (wl_display_roundtrip(wl_dpy) < 0 || wlk_leaves != leaves || wlk_focus != p2.surface) {
        fprintf(stderr, "sibling-grab-exit: the top popup lost the keyboard (leaves %d -> %d)\n", leaves, wlk_leaves);
        return 1;
    }

    // exit with the top grab held: the connection closes on the
    // compositor's side, which destroys every object left
    printf("sibling-grab-exit top grab kept\n");
    return 0;
}
