// Regression: popup grab reopened after dismissal. Open a grab popup,
// let the scenario dismiss it with an outside click, tear it down, then
// open a second one on a fresh serial — it must map and grab like the
// first (stale grab state is the bug this hunts).

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_surface* popup_surface;
static struct xdg_surface* popup_xs;
static struct xdg_popup* popup;
static int popup_committed, popup_gone;
static int generation;

static void popup_xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!popup_committed) {
        wl_surface_attach(popup_surface, wl_solid(120, 90, 0xFFFFFF00), 0, 0);
        wl_surface_damage(popup_surface, 0, 0, 120, 90);
        wl_surface_commit(popup_surface);
        popup_committed = 1;
        printf("popup%d mapped\n", generation);
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)d; (void)p;
    popup_gone = 1;
    printf("popup%d done\n", generation);
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t t) {
    (void)d; (void)p; (void)t;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done,
                                                         popup_repositioned};

static void open_popup(uint32_t serial) {
    generation++;
    popup_committed = 0;
    popup_gone = 0;

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 120, 90);
    xdg_positioner_set_anchor_rect(pos, 20, 20, 60, 20);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);

    popup_surface = wl_compositor_create_surface(wl_comp);
    popup_xs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
    xdg_surface_add_listener(popup_xs, &popup_xs_listener, NULL);
    popup = xdg_surface_get_popup(popup_xs, top.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_popup_grab(popup, wl_seat_g, serial);
    wl_surface_commit(popup_surface);
    xdg_positioner_destroy(pos);
}

static void close_popup(void) {
    xdg_popup_destroy(popup);
    xdg_surface_destroy(popup_xs);
    wl_surface_destroy(popup_surface);
    wl_display_roundtrip(wl_dpy);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (wl_boot()) return 1;

    wl_make_toplevel(&top, "reopen", 300, 200, 0xFFFF0000);
    printf("ready\n");

    for (int round = 1; round <= 2; round++) {
        int base = wlp_button_count;
        while (wlp_button_count <= base && wl_display_dispatch(wl_dpy) != -1) {
        }
        open_popup(wlp_button_serial);

        while (!popup_gone && wl_display_dispatch(wl_dpy) != -1) {
        }
        close_popup();
    }

    printf("reopen ok\n");
    return 0;
}
