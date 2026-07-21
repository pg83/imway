// wl_seat v11: the compositor must advertise the seat at version >= 11 and,
// for a v8+ pointer, deliver high-resolution scroll as axis_value120 (a wheel
// notch = 120) rather than the integer axis_discrete.

#include "wl_util.h"

static struct wl_seat* seat11;
static struct wl_pointer* ptr11;
static uint32_t seat11_version;
static int got_value120, value120_y, got_axis, enter_count;

static void p_enter(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* sf,
                    wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)s; (void)sf; (void)x; (void)y;
    enter_count++;
}
static void p_leave(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* sf) {
    (void)d; (void)p; (void)s; (void)sf;
}
static void p_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)t; (void)x; (void)y;
}
static void p_button(void* d, struct wl_pointer* p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) {
    (void)d; (void)p; (void)s; (void)t; (void)b; (void)st;
}
static void p_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t a, wl_fixed_t v) {
    (void)d; (void)p; (void)t; (void)a; (void)v;
    got_axis = 1;
}
static void p_frame(void* d, struct wl_pointer* p) { (void)d; (void)p; }
static void p_axis_source(void* d, struct wl_pointer* p, uint32_t s) { (void)d; (void)p; (void)s; }
static void p_axis_stop(void* d, struct wl_pointer* p, uint32_t t, uint32_t a) {
    (void)d; (void)p; (void)t; (void)a;
}
static void p_axis_discrete(void* d, struct wl_pointer* p, uint32_t a, int32_t v) {
    (void)d; (void)p; (void)a; (void)v;
}
static void p_axis_value120(void* d, struct wl_pointer* p, uint32_t a, int32_t v) {
    (void)d; (void)p;
    if (a == WL_POINTER_AXIS_VERTICAL_SCROLL) { got_value120 = 1; value120_y = v; }
}
static void p_axis_relative_direction(void* d, struct wl_pointer* p, uint32_t a, uint32_t dir) {
    (void)d; (void)p; (void)a; (void)dir;
}
static const struct wl_pointer_listener p_listener = {
    .enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button,
    .axis = p_axis, .frame = p_frame, .axis_source = p_axis_source,
    .axis_stop = p_axis_stop, .axis_discrete = p_axis_discrete,
    .axis_value120 = p_axis_value120,
    .axis_relative_direction = p_axis_relative_direction,
};

static void seat_caps(void* d, struct wl_seat* s, uint32_t caps) {
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ptr11) {
        ptr11 = wl_seat_get_pointer(s);
        wl_pointer_add_listener(ptr11, &p_listener, NULL);
    }
}
static void seat_name(void* d, struct wl_seat* s, const char* n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_listener = {seat_caps, seat_name};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name,
                        const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, wl_seat_interface.name)) {
        seat11_version = v;
        seat11 = wl_registry_bind(r, name, &wl_seat_interface, v < 11 ? v : 11);
        wl_seat_add_listener(seat11, &seat_listener, NULL);
    }
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (seat11_version < 11) {
        fprintf(stderr, "wl_seat advertised at version %u, want >= 11\n", seat11_version);
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "seat11", 300, 200, 0xFF2060C0u);
    printf("client_reg_seat_version_11: mapped\n");

    // let the scenario move the pointer over us and scroll
    while (wl_display_dispatch(wl_dpy) != -1) {
        if (got_value120) {
            if (value120_y == 0) {
                fprintf(stderr, "axis_value120 delivered zero\n");
                return 1;
            }
            printf("client_reg_seat_version_11: value120 %d\n", value120_y);
            return 0;
        }
        if (got_axis && enter_count) {
            // an axis arrived but no value120: v8 contract unmet
            fprintf(stderr, "scroll delivered without axis_value120\n");
            return 1;
        }
    }
    return 1;
}
