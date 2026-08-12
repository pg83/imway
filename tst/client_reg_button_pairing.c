// Regression: button press/release pairing across the client/chrome border.
// Phase A: the scenario presses over our content, drags the pointer out onto
// the empty desktop and releases there — the implicit grab must deliver the
// release (and motion must keep flowing while held). Phase B: a right-button
// press lands on the SSD title bar (compositor chrome) and the release over
// our content — neither may reach us, an orphan release is the bug. The
// scenario ends phase B with the sentinel key (KEY_M); we then report the
// phase-B button count and exit.

#include "wl_util.h"
#include <linux/input-event-codes.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>

static struct zxdg_decoration_manager_v1* deco_mgr;
static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int cur_w = 300, cur_h = 200;
static int pend_w, pend_h;
static int mapped_printed;

static int presses, releases;
static int held, held_motions, held_motion_outside;

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (pend_w > 0) cur_w = pend_w;
    if (pend_h > 0) cur_h = pend_h;
    wl_surface_attach(surface, wl_solid(cur_w, cur_h, 0xFF0000FF), 0, 0);
    wl_surface_damage(surface, 0, 0, cur_w, cur_h);
    wl_surface_commit(surface);
    if (!mapped_printed) {
        printf("pairing ready\n");
        mapped_printed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t; (void)states;
    pend_w = w;
    pend_h = h;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

// own wl_pointer: wl_util's listener only records the last event, pairing
// needs separate press/release counts and held-motion tracking
static void p2_enter(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s,
                     wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)serial; (void)s;
    printf("enter %.0f,%.0f\n", wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void p2_leave(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s) {
    (void)d; (void)p; (void)serial; (void)s;
}
static void p2_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)t;
    if (held) {
        held_motions++;
        double dx = wl_fixed_to_double(x), dy = wl_fixed_to_double(y);
        if (dx < 0 || dy < 0 || dx > cur_w || dy > cur_h) held_motion_outside = 1;
    }
}
static void p2_button(void* d, struct wl_pointer* p, uint32_t serial, uint32_t t,
                      uint32_t button, uint32_t state) {
    (void)d; (void)p; (void)serial; (void)t;
    printf("button %u state %u\n", button, state);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        presses++;
        if (button == BTN_LEFT) held = 1;
    } else {
        releases++;
        if (button == BTN_LEFT) held = 0;
    }
}
static void p2_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t a, wl_fixed_t v) {
    (void)d; (void)p; (void)t; (void)a; (void)v;
}
static void p2_frame(void* d, struct wl_pointer* p) { (void)d; (void)p; }
static void p2_axis_src(void* d, struct wl_pointer* p, uint32_t s) { (void)d; (void)p; (void)s; }
static void p2_axis_stop(void* d, struct wl_pointer* p, uint32_t t, uint32_t a) {
    (void)d; (void)p; (void)t; (void)a;
}
static void p2_axis_disc(void* d, struct wl_pointer* p, uint32_t a, int32_t v) {
    (void)d; (void)p; (void)a; (void)v;
}
static const struct wl_pointer_listener p2_listener = {
    .enter = p2_enter, .leave = p2_leave, .motion = p2_motion, .button = p2_button,
    .axis = p2_axis, .frame = p2_frame, .axis_source = p2_axis_src,
    .axis_stop = p2_axis_stop, .axis_discrete = p2_axis_disc,
};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;
    if (!wl_ptr) { fprintf(stderr, "no pointer\n"); return 1; }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!deco_mgr) { fprintf(stderr, "no xdg-decoration manager\n"); return 1; }

    struct wl_pointer* ptr2 = wl_seat_get_pointer(wl_seat_g);
    wl_pointer_add_listener(ptr2, &p2_listener, NULL);
    wlk_watch_key = KEY_M; // the scenario's phase-B sentinel

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "pairing");
    xdg_toplevel_set_app_id(tl, "pairing");

    struct zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, tl);
    zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    wl_surface_commit(surface);

    // phase A: exactly one press and one release must arrive, with motion
    // still flowing (and reaching outside coords) while the button is held
    while ((presses < 1 || releases < 1) && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (presses != 1 || releases != 1) {
        fprintf(stderr, "pairA broken: presses=%d releases=%d\n", presses, releases);
        return 1;
    }
    if (!held_motions || !held_motion_outside) {
        fprintf(stderr, "pairA broken: no implicit-grab motion (motions=%d outside=%d)\n",
                held_motions, held_motion_outside);
        return 1;
    }
    printf("pairA ok\n");

    // phase B: nothing with buttons may arrive until the sentinel key
    int base_presses = presses, base_releases = releases;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    int extra = (presses - base_presses) + (releases - base_releases);
    printf("pairB extra_buttons=%d\n", extra);
    if (extra) {
        fprintf(stderr, "pairB broken: %d orphan button event(s) leaked to the client\n", extra);
        return 1;
    }
    printf("pairing ok\n");
    return 0;
}
