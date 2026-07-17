// Regression (#13): xdg_popup.grab used to accept only a pointer-button
// serial, so a menu opened from the keyboard (Menu key, Shift+F10) got an
// INVALID_GRAB protocol error and the client was killed. The grab must also
// accept the last key-press serial. Repro: map a toplevel (it takes keyboard
// focus), wait for an injected key, then grab a popup off that key serial.
// Success = the popup maps and the client is not disconnected.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_surface* popup_surface;
static struct xdg_popup* popup;
static int popup_committed;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_reposition(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done,
                                                         popup_reposition};

static void popup_xdg_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!popup_committed) {
        wl_surface_attach(popup_surface, wl_solid(120, 90, 0xFFFFFF00), 0, 0);
        wl_surface_commit(popup_surface);
        popup_committed = 1;
        printf("client_reg_popup_key_serial: popup committed\n");
    }
}
static const struct xdg_surface_listener popup_xdg_listener = {popup_xdg_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    wl_make_toplevel(&top, "client_reg_popup_key_serial", 400, 300, 0xFFFF0000);
    printf("client_reg_popup_key_serial: mapped\n");

    // wait for an injected key, then grab a popup off that key serial
    for (int i = 0; i < 300; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) {
            // the compositor disconnected us — that is the pre-fix INVALID_GRAB
            fprintf(stderr, "client_reg_popup_key_serial: disconnected\n");
            return 1;
        }

        if (!popup && wlk_key_serial) {
            struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
            xdg_positioner_set_size(pos, 120, 90);
            xdg_positioner_set_anchor_rect(pos, 20, 20, 60, 20);
            popup_surface = wl_compositor_create_surface(wl_comp);
            struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
            xdg_surface_add_listener(pxs, &popup_xdg_listener, NULL);
            popup = xdg_surface_get_popup(pxs, top.xs, pos);
            xdg_popup_add_listener(popup, &popup_listener, NULL);
            xdg_popup_grab(popup, wl_seat_g, wlk_key_serial); // the key serial
            xdg_positioner_destroy(pos);
            wl_surface_commit(popup_surface);
            printf("client_reg_popup_key_serial: grabbed on key serial %u\n", wlk_key_serial);
        }

        usleep(20000);
    }

    // survived the grab request without being disconnected
    return 0;
}
