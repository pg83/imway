// Grabs asked for on serials that no longer hold an implicit grab. A red
// window with a green subsurface on its right half:
//   pressed on the window, a popup grab on a serial that is not the press's
//   is dismissed at once;
//   released, a popup grab on the press's own serial is dismissed as well;
//   pressed on the subsurface, which the client then destroys with the
//   button still down, a popup grab and a move on that press's serial are
//   refused, the popup dismissed and the window left where it is.
// Every step waits for the scenario's press or release and prints its
// outcome; the client exits on the go-exit file.

#include "wl_util.h"

static int dismissed;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)d; (void)p;
    dismissed = 1;
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static struct wl_toplevel_ctx top;

// a grab popup on <serial>; true when the compositor dismissed it
static int grab_dismissed(uint32_t serial) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 60, 40);
    xdg_positioner_set_anchor_rect(pos, 10, 10, 20, 20);
    struct wl_surface* s = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, s);
    xdg_surface_add_listener(xs, &popup_xs_listener, NULL);
    struct xdg_popup* p = xdg_surface_get_popup(xs, top.xs, pos);
    xdg_popup_add_listener(p, &popup_listener, NULL);
    xdg_positioner_destroy(pos);
    dismissed = 0;
    xdg_popup_grab(p, wl_seat_g, serial);
    wl_surface_commit(s);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    int done = dismissed;

    xdg_popup_destroy(p);
    xdg_surface_destroy(xs);
    wl_surface_destroy(s);
    wl_display_roundtrip(wl_dpy);
    return done;
}

static void wait_button(uint32_t state) {
    int seen = wlp_button_count;
    while ((wlp_button_count == seen || wlp_button_state != state) && wl_display_dispatch(wl_dpy) != -1) {
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (wl_boot() || !wl_ptr || !wl_subcomp) return 2;

    wl_make_toplevel(&top, "grab-serial-edges", 300, 200, 0xFFFF0000);

    struct wl_surface* sub = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* subsurface = wl_subcompositor_get_subsurface(wl_subcomp, sub, top.surface);
    wl_subsurface_set_position(subsurface, 150, 0);
    wl_subsurface_set_desync(subsurface);
    wl_surface_attach(sub, wl_solid(150, 200, 0xFF00FF00), 0, 0);
    wl_surface_damage(sub, 0, 0, 150, 200);
    wl_surface_commit(sub);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("ready\n");

    wait_button(WL_POINTER_BUTTON_STATE_PRESSED);
    uint32_t press = wlp_button_serial;
    if (!grab_dismissed(press + 1)) {
        fprintf(stderr, "a grab on a serial that is not the press's was kept\n");
        return 1;
    }
    printf("wrong serial dismissed\n");

    wait_button(WL_POINTER_BUTTON_STATE_RELEASED);
    if (!grab_dismissed(press)) {
        fprintf(stderr, "a grab on a released press was kept\n");
        return 1;
    }
    printf("released dismissed\n");

    wait_button(WL_POINTER_BUTTON_STATE_PRESSED);
    press = wlp_button_serial;
    if (wlp_focus != sub) {
        fprintf(stderr, "the second press was not on the subsurface\n");
        return 1;
    }
    wl_subsurface_destroy(subsurface);
    wl_surface_destroy(sub);
    wl_display_roundtrip(wl_dpy);
    if (!grab_dismissed(press)) {
        fprintf(stderr, "a grab on a press whose surface is gone was kept\n");
        return 1;
    }
    xdg_toplevel_move(top.tl, wl_seat_g, press);
    wl_display_roundtrip(wl_dpy);
    // the release that ends this press goes to no surface of ours: the
    // pressed one is gone. The window stays until the scenario has seen
    // that the pointer does not carry it
    printf("origin gone dismissed\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/go-exit", getenv("XDG_RUNTIME_DIR"));
    while (access(path, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }
    return 0;
}
