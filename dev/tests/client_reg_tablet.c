// tablet-v2: the virtual pen must announce itself on the tablet seat and
// deliver proximity/down/motion/pressure/up frames with surface-local
// coordinates to the surface under the tool.

#include "wl_util.h"
#include <tablet-v2-client-protocol.h>

static struct zwp_tablet_manager_v2* tablet_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_tablet_manager_v2_interface.name))
        tablet_mgr = wl_registry_bind(r, name, &zwp_tablet_manager_v2_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_surface* my_surface;
static struct zwp_tablet_v2* tablet_dev;
static struct zwp_tablet_tool_v2* tool_dev;
static int tool_ready;

// per-frame accumulation, printed on frame()
static int saw_prox_in, saw_prox_out, saw_down, saw_up, saw_motion;
static int motion_x, motion_y;
static uint32_t pressure_v; static int saw_pressure;

static void tab_name(void* d, struct zwp_tablet_v2* t, const char* n) { (void)d;(void)t;(void)n; }
static void tab_id(void* d, struct zwp_tablet_v2* t, uint32_t hi, uint32_t lo) { (void)d;(void)t;(void)hi;(void)lo; }
static void tab_path(void* d, struct zwp_tablet_v2* t, const char* p) { (void)d;(void)t;(void)p; }
static void tab_done(void* d, struct zwp_tablet_v2* t) { (void)d;(void)t; }
static void tab_removed(void* d, struct zwp_tablet_v2* t) { (void)d;(void)t; }
static const struct zwp_tablet_v2_listener tablet_listener = {
    tab_name, tab_id, tab_path, tab_done, tab_removed,
};

static void tool_type(void* d, struct zwp_tablet_tool_v2* t, uint32_t type) {
    (void)d; (void)t;
    if (type != ZWP_TABLET_TOOL_V2_TYPE_PEN) {
        fprintf(stderr, "unexpected tool type 0x%x\n", type);
        exit(1);
    }
}
static void tool_serial(void* d, struct zwp_tablet_tool_v2* t, uint32_t hi, uint32_t lo) { (void)d;(void)t;(void)hi;(void)lo; }
static void tool_id_wacom(void* d, struct zwp_tablet_tool_v2* t, uint32_t hi, uint32_t lo) { (void)d;(void)t;(void)hi;(void)lo; }
static void tool_cap(void* d, struct zwp_tablet_tool_v2* t, uint32_t cap) { (void)d;(void)t;(void)cap; }
static void tool_done(void* d, struct zwp_tablet_tool_v2* t) {
    (void)d; (void)t;
    tool_ready = 1;
}
static void tool_removed(void* d, struct zwp_tablet_tool_v2* t) { (void)d;(void)t; }
static void tool_prox_in(void* d, struct zwp_tablet_tool_v2* t, uint32_t serial,
                         struct zwp_tablet_v2* tab, struct wl_surface* surf) {
    (void)d; (void)t; (void)serial; (void)tab;
    if (surf != my_surface) {
        fprintf(stderr, "proximity_in on a foreign surface\n");
        exit(1);
    }
    saw_prox_in = 1;
}
static void tool_prox_out(void* d, struct zwp_tablet_tool_v2* t) { (void)d;(void)t; saw_prox_out = 1; }
static void tool_down(void* d, struct zwp_tablet_tool_v2* t, uint32_t serial) { (void)d;(void)t;(void)serial; saw_down = 1; }
static void tool_up(void* d, struct zwp_tablet_tool_v2* t) { (void)d;(void)t; saw_up = 1; }
static void tool_motion(void* d, struct zwp_tablet_tool_v2* t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)t;
    saw_motion = 1;
    motion_x = wl_fixed_to_int(x);
    motion_y = wl_fixed_to_int(y);
}
static void tool_pressure(void* d, struct zwp_tablet_tool_v2* t, uint32_t p) { (void)d;(void)t; saw_pressure = 1; pressure_v = p; }
static void tool_distance(void* d, struct zwp_tablet_tool_v2* t, uint32_t v) { (void)d;(void)t;(void)v; }
static void tool_tilt(void* d, struct zwp_tablet_tool_v2* t, wl_fixed_t x, wl_fixed_t y) { (void)d;(void)t;(void)x;(void)y; }
static void tool_rotation(void* d, struct zwp_tablet_tool_v2* t, wl_fixed_t r) { (void)d;(void)t;(void)r; }
static void tool_slider(void* d, struct zwp_tablet_tool_v2* t, int32_t v) { (void)d;(void)t;(void)v; }
static void tool_wheel(void* d, struct zwp_tablet_tool_v2* t, wl_fixed_t deg, int32_t clicks) { (void)d;(void)t;(void)deg;(void)clicks; }
static void tool_button(void* d, struct zwp_tablet_tool_v2* t, uint32_t serial, uint32_t button, uint32_t state) {
    (void)d;(void)t;(void)serial;(void)button;(void)state;
}
static void tool_frame(void* d, struct zwp_tablet_tool_v2* t, uint32_t time) {
    (void)d; (void)t; (void)time;
    if (saw_prox_in) printf("tablet: prox_in\n");
    if (saw_down) printf("tablet: down\n");
    if (saw_up) printf("tablet: up\n");
    if (saw_motion) printf("tablet: motion %d %d\n", motion_x, motion_y);
    if (saw_pressure) printf("tablet: pressure %u\n", pressure_v);
    if (saw_prox_out) printf("tablet: prox_out\n");
    saw_prox_in = saw_prox_out = saw_down = saw_up = saw_motion = saw_pressure = 0;
}
static const struct zwp_tablet_tool_v2_listener tool_listener = {
    tool_type, tool_serial, tool_id_wacom, tool_cap, tool_done, tool_removed,
    tool_prox_in, tool_prox_out, tool_down, tool_up, tool_motion,
    tool_pressure, tool_distance, tool_tilt, tool_rotation, tool_slider,
    tool_wheel, tool_button, tool_frame,
};

static void seat_tablet_added(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_v2* t) {
    (void)d; (void)s;
    tablet_dev = t;
    zwp_tablet_v2_add_listener(t, &tablet_listener, NULL);
}
static void seat_tool_added(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_tool_v2* t) {
    (void)d; (void)s;
    tool_dev = t;
    zwp_tablet_tool_v2_add_listener(t, &tool_listener, NULL);
}
static void seat_pad_added(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_pad_v2* p) {
    (void)d; (void)s; (void)p;
}
static const struct zwp_tablet_seat_v2_listener tablet_seat_listener = {
    seat_tablet_added, seat_tool_added, seat_pad_added,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!tablet_mgr) {
        fprintf(stderr, "no zwp_tablet_manager_v2\n");
        return 1;
    }

    struct zwp_tablet_seat_v2* tseat =
        zwp_tablet_manager_v2_get_tablet_seat(tablet_mgr, wl_seat_g);
    zwp_tablet_seat_v2_add_listener(tseat, &tablet_seat_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy); // tablet_added burst, then tablet/tool events

    if (!tablet_dev || !tool_dev || !tool_ready) {
        fprintf(stderr, "tablet seat did not announce a tablet + tool\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "tablet-test", 400, 300, 0xFF3060A0u);
    my_surface = top.surface;
    printf("client_reg_tablet: tool ready\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
