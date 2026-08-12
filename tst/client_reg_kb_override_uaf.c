// Regression (#3): a grab popup sets the seat's kbOverride to the popup
// surface. If the client destroys that wl_surface before the xdg_popup role,
// surfaceGone must clear kbOverride and hand keyboard focus back to the
// toplevel. The arena allocator keeps the freed surface readable, so the old
// dangling-read does not necessarily crash — instead we assert the observable
// contract: after the popup surface dies, the toplevel gets keyboard focus
// again. Pre-fix kbOverride stayed pinned to the dead popup and no such
// re-focus was delivered.

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
    popup = xdg_surface_get_popup(pxs, top.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_popup_grab(popup, wl_seat_g, serial);
    xdg_positioner_destroy(pos);
    wl_surface_commit(popup_surface);
}

enum { WAIT_BUTTON, WAIT_GRAB_FOCUS, WAIT_REFOCUS };

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_kbd || !wl_ptr) {
        fprintf(stderr, "client_reg_kb_override_uaf: no keyboard/pointer\n");
        return 1;
    }

    wl_make_toplevel(&top, "client_reg_kb_override_uaf", 400, 300, 0xFFFF0000); // red
    printf("client_reg_kb_override_uaf: mapped\n");

    int phase = WAIT_BUTTON;

    for (int i = 0; i < 400; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;

        if (phase == WAIT_BUTTON && wlp_button_count > 0 &&
            wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
            open_grab_popup(wlp_button_serial);
            phase = WAIT_GRAB_FOCUS;
        } else if (phase == WAIT_GRAB_FOCUS && popup_committed && wlk_focus == popup_surface) {
            // the grab moved keyboard focus onto the popup; now destroy the
            // wl_surface out from under the grab, before the xdg_popup role
            wl_surface_destroy(popup_surface);
            popup_surface = NULL;
            printf("client_reg_kb_override_uaf: popup surface destroyed\n");
            phase = WAIT_REFOCUS;
        } else if (phase == WAIT_REFOCUS && wlk_focus == top.surface) {
            printf("client_reg_kb_override_uaf: toplevel refocused after teardown\n");
            return 0;
        }

        usleep(20000);
    }

    fprintf(stderr, "client_reg_kb_override_uaf: no refocus (phase=%d) — kbOverride dangled\n",
            phase);
    return 1;
}
