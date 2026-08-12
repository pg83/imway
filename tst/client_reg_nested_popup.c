// Regression (#26): grab popups nest in a stack. Dismissing the deepest popup
// must return the keyboard to its parent popup, not skip straight to the
// toplevel. Repro: open popup1 (grab, child of toplevel), popup2 (grab, child
// of popup1); keyboard focus is on popup2. Destroy popup2 and assert focus
// returns to popup1.

#include "wl_util.h"

static struct wl_toplevel_ctx top;

struct pop {
    struct wl_surface* surface;
    struct xdg_surface* xs;
    struct xdg_popup* popup;
    int committed;
};
static struct pop p1, p2;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_reposition(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done,
                                                         popup_reposition};

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

// pump until a flag predicate holds, bounded
#define PUMP_UNTIL(cond)                                        \
    do {                                                       \
        for (int _i = 0; _i < 200 && !(cond); _i++) {          \
            if (wl_display_roundtrip(wl_dpy) < 0) break;       \
            usleep(20000);                                     \
        }                                                      \
    } while (0)

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_kbd || !wl_ptr) {
        fprintf(stderr, "client_reg_nested_popup: no keyboard/pointer\n");
        return 1;
    }

    wl_make_toplevel(&top, "client_reg_nested_popup", 400, 300, 0xFFFF0000);
    printf("client_reg_nested_popup: mapped\n");

    // click to arm a grab serial
    PUMP_UNTIL(wlp_button_count > 0 && wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED);
    if (!wlp_button_serial) {
        fprintf(stderr, "client_reg_nested_popup: no pointer button\n");
        return 1;
    }
    uint32_t serial = wlp_button_serial;

    open_popup(&p1, top.xs, serial);
    PUMP_UNTIL(p1.committed && wlk_focus == p1.surface);
    if (wlk_focus != p1.surface) {
        fprintf(stderr, "client_reg_nested_popup: popup1 never got keyboard focus\n");
        return 1;
    }

    open_popup(&p2, p1.xs, serial);
    PUMP_UNTIL(p2.committed && wlk_focus == p2.surface);
    if (wlk_focus != p2.surface) {
        fprintf(stderr, "client_reg_nested_popup: popup2 never got keyboard focus\n");
        return 1;
    }
    printf("client_reg_nested_popup: both popups grabbed\n");

    // dismiss the deepest popup
    xdg_popup_destroy(p2.popup);
    wl_surface_destroy(p2.surface);
    p2.surface = NULL;

    PUMP_UNTIL(wlk_focus == p1.surface);

    if (wlk_focus != p1.surface) {
        fprintf(stderr, "client_reg_nested_popup: focus did not return to popup1 (%p, top=%p)\n",
                (void*)wlk_focus, (void*)top.surface);
        return 1;
    }

    printf("client_reg_nested_popup: focus returned to the parent popup\n");
    return 0;
}
