// Regression (#17): while a popup grab holds the keyboard (kbOverride set),
// mapping a new toplevel must not send it a keyboard enter — the client would
// otherwise see two focused surfaces while keys still go to the popup. Repro:
// map A, open a grab popup (keyboard focus moves to it), then map B and assert
// the keyboard focus stays on the popup, not B.

#include "wl_util.h"

static struct wl_toplevel_ctx a, b;
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
    }
}
static const struct xdg_surface_listener popup_xdg_listener = {popup_xdg_configure};

static void open_grab_popup(uint32_t serial) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 120, 90);
    xdg_positioner_set_anchor_rect(pos, 20, 20, 60, 20);
    popup_surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
    xdg_surface_add_listener(pxs, &popup_xdg_listener, NULL);
    popup = xdg_surface_get_popup(pxs, a.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_popup_grab(popup, wl_seat_g, serial);
    xdg_positioner_destroy(pos);
    wl_surface_commit(popup_surface);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_kbd || !wl_ptr) {
        fprintf(stderr, "client_reg_focus_vs_grab: no keyboard/pointer\n");
        return 1;
    }

    wl_make_toplevel(&a, "client_reg_focus_vs_grab_A", 400, 300, 0xFFFF0000); // red
    printf("client_reg_focus_vs_grab: mapped\n");

    // wait for the pointer click, open the grab popup, wait until keyboard
    // focus is on the popup
    int grabbed = 0;
    for (int i = 0; i < 300 && !grabbed; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        if (!popup && wlp_button_count > 0 &&
            wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
            open_grab_popup(wlp_button_serial);
        }
        if (popup_committed && wlk_focus == popup_surface) grabbed = 1;
        usleep(20000);
    }
    if (!grabbed) {
        fprintf(stderr, "client_reg_focus_vs_grab: popup grab never took keyboard focus\n");
        return 1;
    }
    printf("client_reg_focus_vs_grab: popup has keyboard focus\n");

    // map a second toplevel — its map calls focusToplevel(B)
    wl_make_toplevel(&b, "client_reg_focus_vs_grab_B", 400, 300, 0xFF0000FF); // blue
    printf("client_reg_focus_vs_grab: mapped B\n");

    // give the compositor time to (wrongly) hand focus to B
    for (int i = 0; i < 40; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }

    if (wlk_focus == b.surface) {
        fprintf(stderr, "client_reg_focus_vs_grab: keyboard focus jumped to B under grab\n");
        return 1;
    }
    if (wlk_focus != popup_surface) {
        fprintf(stderr, "client_reg_focus_vs_grab: keyboard focus left the popup (%p)\n",
                (void*)wlk_focus);
        return 1;
    }

    printf("client_reg_focus_vs_grab: focus stayed on the popup\n");
    return 0;
}
