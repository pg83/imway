// A click on a grabbing popup goes to the popup and keeps it open; a click
// on its parent window dismisses it. Above the grabbing popup sit a plain
// child popup without a grab and a popup that never got a buffer: neither
// of them takes part in the click-outside dismissal.

#include "wl_util.h"

static struct wl_toplevel_ctx top;

struct pop {
    struct wl_surface* surface;
    struct xdg_surface* xs;
    struct xdg_popup* popup;
    uint32_t color;
    int w, h;
    int buffer; // attach a buffer on the first configure
    int committed, done;
};
static struct pop grabbing, plain, empty;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)p;
    ((struct pop*)d)->done = 1;
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct pop* pp = d;

    xdg_surface_ack_configure(xs, serial);

    if (pp->buffer && !pp->committed) {
        wl_surface_attach(pp->surface, wl_solid(pp->w, pp->h, pp->color), 0, 0);
        wl_surface_commit(pp->surface);
        pp->committed = 1;
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void open_popup(struct pop* pp, struct xdg_surface* parent, int ax, int ay, int grab, uint32_t serial) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

    xdg_positioner_set_size(pos, pp->w, pp->h);
    xdg_positioner_set_anchor_rect(pos, ax, ay, 1, 1);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    pp->surface = wl_compositor_create_surface(wl_comp);
    pp->xs = xdg_wm_base_get_xdg_surface(wl_wm, pp->surface);
    xdg_surface_add_listener(pp->xs, &popup_xs_listener, pp);
    pp->popup = xdg_surface_get_popup(pp->xs, parent, pos);
    xdg_popup_add_listener(pp->popup, &popup_listener, pp);

    if (grab) {
        xdg_popup_grab(pp->popup, wl_seat_g, serial);
    }

    xdg_positioner_destroy(pos);
    wl_surface_commit(pp->surface);
}

#define PUMP_UNTIL(cond)                                        \
    do {                                                       \
        for (int _i = 0; _i < 1000 && !(cond); _i++) {         \
            if (wl_display_roundtrip(wl_dpy) < 0) break;       \
            usleep(20000);                                     \
        }                                                      \
    } while (0)

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) return 1;
    if (!wl_kbd || !wl_ptr) {
        fprintf(stderr, "no keyboard/pointer\n");
        return 1;
    }

    wl_make_toplevel(&top, "popup-grab-click", 400, 300, 0xFFFF0000);
    printf("grab-click mapped\n");

    PUMP_UNTIL(wlp_button_count > 0 && wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED);
    if (!wlp_button_serial) {
        fprintf(stderr, "no pointer button\n");
        return 1;
    }

    uint32_t serial = wlp_button_serial;

    grabbing = (struct pop){.color = 0xFFFFFF00, .w = 160, .h = 120, .buffer = 1};
    open_popup(&grabbing, top.xs, 10, 10, 1, serial);
    PUMP_UNTIL(grabbing.committed && wlk_focus == grabbing.surface);
    if (wlk_focus != grabbing.surface) {
        fprintf(stderr, "the grabbing popup never took the keyboard\n");
        return 1;
    }

    // the plain child covers only the grabbing popup's far corner, so the
    // popup's middle is its own
    plain = (struct pop){.color = 0xFF00FF00, .w = 40, .h = 30, .buffer = 1};
    open_popup(&plain, grabbing.xs, 150, 110, 0, 0);
    PUMP_UNTIL(plain.committed);

    empty = (struct pop){.w = 50, .h = 50, .buffer = 0};
    open_popup(&empty, top.xs, 300, 200, 0, 0);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    int presses = wlp_button_count;

    printf("popups open\n");

    PUMP_UNTIL(wlp_focus == grabbing.surface || grabbing.done);
    if (grabbing.done) {
        fprintf(stderr, "the grabbing popup was dismissed before the click on it\n");
        return 1;
    }
    printf("pointer on the popup\n");

    // the release of the arming press, then the click on the popup: had
    // that click gone anywhere but the popup, the popup would be done
    PUMP_UNTIL(grabbing.done || (wlp_button_count >= presses + 3 && wlp_focus == grabbing.surface));
    wl_display_roundtrip(wl_dpy);
    if (grabbing.done || plain.done) {
        fprintf(stderr, "a click on the grabbing popup dismissed it\n");
        return 1;
    }
    printf("popup clicked, still open\n");

    PUMP_UNTIL(wlp_focus == top.surface || grabbing.done);
    if (grabbing.done) {
        fprintf(stderr, "the grabbing popup was dismissed before the click on the window\n");
        return 1;
    }
    printf("pointer on the window\n");

    PUMP_UNTIL(grabbing.done);
    if (!grabbing.done) {
        fprintf(stderr, "a click on the window left the grabbing popup open\n");
        return 1;
    }
    printf("popup dismissed by a click on the window\n");

    xdg_popup_destroy(empty.popup);
    xdg_popup_destroy(plain.popup);
    xdg_popup_destroy(grabbing.popup);
    wl_display_roundtrip(wl_dpy);
    return 0;
}
