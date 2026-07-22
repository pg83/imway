// xdg-toplevel-drag: a toplevel attached to a data-source drag follows the
// cursor. Start a pointer drag from a window, attach a second toplevel to it,
// move the pointer, and the attached window must track the cursor (observed
// through the compositor's window position in the state dump).

#include "wl_util.h"
#include <xdg-toplevel-drag-v1-client-protocol.h>

static struct xdg_toplevel_drag_manager_v1* drag_mgr;
static struct wl_data_device_manager* ddm;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_toplevel_drag_manager_v1_interface.name))
        drag_mgr = wl_registry_bind(r, name, &xdg_toplevel_drag_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_data_device_manager_interface.name))
        ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, 3);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static uint32_t enter_serial;
static void p_enter(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s,
                    wl_fixed_t x, wl_fixed_t y) {
    (void)d;(void)p;(void)s;(void)x;(void)y;
    enter_serial = serial;
}
static void p_leave(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s) {
    (void)d;(void)p;(void)serial;(void)s;
}
static void p_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d;(void)p;(void)t;(void)x;(void)y;
}
static uint32_t btn_serial; static int pressed_seen;
static void p_button(void* d, struct wl_pointer* p, uint32_t serial, uint32_t t,
                     uint32_t button, uint32_t state) {
    (void)d;(void)p;(void)t;(void)button;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        btn_serial = serial;
        pressed_seen = 1;
    }
}
static void p_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t a, wl_fixed_t v) {
    (void)d;(void)p;(void)t;(void)a;(void)v;
}
static void p_frame(void* d, struct wl_pointer* p) { (void)d;(void)p; }
static void p_axis_src(void* d, struct wl_pointer* p, uint32_t s) { (void)d;(void)p;(void)s; }
static void p_axis_stop(void* d, struct wl_pointer* p, uint32_t t, uint32_t a) { (void)d;(void)p;(void)t;(void)a; }
static void p_axis_disc(void* d, struct wl_pointer* p, uint32_t a, int32_t v) { (void)d;(void)p;(void)a;(void)v; }
static const struct wl_pointer_listener pl = {
    p_enter, p_leave, p_motion, p_button, p_axis,
    p_frame, p_axis_src, p_axis_stop, p_axis_disc,
};

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d;(void)s;(void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d;(void)s;(void)m; close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d;(void)s; }
static void src_dnd_drop(void* d, struct wl_data_source* s) { (void)d;(void)s; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d;(void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d;(void)s;(void)a; }
static const struct wl_data_source_listener sl = {
    src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!drag_mgr || !ddm || !wl_seat_g) {
        fprintf(stderr, "missing globals (drag=%p ddm=%p seat=%p)\n",
                (void*)drag_mgr, (void*)ddm, (void*)wl_seat_g);
        return 1;
    }

    struct wl_pointer* ptr = wl_seat_get_pointer(wl_seat_g);
    wl_pointer_add_listener(ptr, &pl, NULL);

    // the origin window (the "tab bar") and the window to be torn off
    struct wl_toplevel_ctx origin, torn;
    wl_make_toplevel(&origin, "drag-origin", 400, 300, 0xFF3060A0u);
    wl_make_toplevel(&torn, "drag-torn", 240, 160, 0xFFA06030u);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_toplevel_drag: ready\n");

    // the scenario presses the pointer on the origin, then releases the client
    wlk_watch_key = 57; // KEY_SPACE
    while ((!pressed_seen || !btn_serial) && wl_display_dispatch(wl_dpy) != -1) {
        if (wlk_watch_hits) break;
    }
    if (!btn_serial) {
        fprintf(stderr, "no pointer button serial (press did not land on origin)\n");
        return 1;
    }

    struct wl_data_device* dev = wl_data_device_manager_get_data_device(ddm, wl_seat_g);
    struct wl_data_source* src = wl_data_device_manager_create_data_source(ddm);
    wl_data_source_add_listener(src, &sl, NULL);
    wl_data_source_offer(src, "text/plain");

    struct xdg_toplevel_drag_v1* drag =
        xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag(drag_mgr, src);

    wl_data_device_start_drag(dev, src, origin.surface, NULL, btn_serial);
    // attach the torn window with a 20,10 offset from the cursor hotspot
    xdg_toplevel_drag_v1_attach(drag, torn.tl, 20, 10);
    wl_display_roundtrip(wl_dpy);

    printf("client_reg_toplevel_drag: dragging\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
