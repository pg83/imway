// Regression (#13): xdg_popup.grab used to accept only a pointer-button
// serial, so a menu opened from the keyboard (Menu key, Shift+F10) got an
// INVALID_GRAB protocol error and the client was killed. The grab must also
// accept the last key-press serial. Repro: map a toplevel (it takes keyboard
// focus), wait for an injected key, then grab a popup off that key serial.
// Success = the popup takes the grab (it gets the keyboard, no popup_done)
// and the client is not disconnected. The grab uses the press serial: the
// scenario's release follows at once, and a release is no grab trigger.
// First, a grab off a serial one short of the press is dismissed: the key
// grab is the press's serial, not any serial of the focused client.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_surface* popup_surface;
static struct xdg_popup* popup;
static int popup_committed, popup_dismissed;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)p;
    *(int*)d = 1;
}
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

        if (!popup && wlk_press_serial) {
            // a serial the key grab never had: dismissed, not an error
            int stale_dismissed = 0;
            struct xdg_positioner* spos = xdg_wm_base_create_positioner(wl_wm);
            xdg_positioner_set_size(spos, 120, 90);
            xdg_positioner_set_anchor_rect(spos, 20, 20, 60, 20);
            struct wl_surface* ss = wl_compositor_create_surface(wl_comp);
            struct xdg_surface* sxs = xdg_wm_base_get_xdg_surface(wl_wm, ss);
            struct xdg_popup* stale = xdg_surface_get_popup(sxs, top.xs, spos);
            xdg_popup_add_listener(stale, &popup_listener, &stale_dismissed);
            xdg_popup_grab(stale, wl_seat_g, wlk_press_serial - 1);
            xdg_positioner_destroy(spos);
            while (!stale_dismissed && wl_display_dispatch(wl_dpy) != -1) {
            }
            if (!stale_dismissed) {
                fprintf(stderr, "client_reg_popup_key_serial: disconnected on a stale serial\n");
                return 1;
            }
            xdg_popup_destroy(stale);
            xdg_surface_destroy(sxs);
            wl_surface_destroy(ss);
            printf("client_reg_popup_key_serial: stale serial dismissed\n");

            struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
            xdg_positioner_set_size(pos, 120, 90);
            xdg_positioner_set_anchor_rect(pos, 20, 20, 60, 20);
            popup_surface = wl_compositor_create_surface(wl_comp);
            struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
            xdg_surface_add_listener(pxs, &popup_xdg_listener, NULL);
            popup = xdg_surface_get_popup(pxs, top.xs, pos);
            xdg_popup_add_listener(popup, &popup_listener, &popup_dismissed);
            xdg_popup_grab(popup, wl_seat_g, wlk_press_serial); // the key serial
            xdg_positioner_destroy(pos);
            wl_surface_commit(popup_surface);
            printf("client_reg_popup_key_serial: grabbed on key serial %u\n", wlk_press_serial);
        }

        if (popup_dismissed) {
            fprintf(stderr, "client_reg_popup_key_serial: the key-serial grab was refused\n");
            return 1;
        }

        if (popup && popup_committed && wlk_focus == popup_surface) {
            printf("client_reg_popup_key_serial: popup holds the keyboard\n");
            return 0;
        }

        usleep(20000);
    }

    fprintf(stderr, "client_reg_popup_key_serial: the popup never got the keyboard\n");
    return 1;
}
