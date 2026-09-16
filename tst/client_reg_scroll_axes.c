/* Scroll events at wl_seat v11: the axis value itself, the high-resolution
 * value120 the wheel carries, the relative direction v9 added, the source,
 * the stop event and the frame that closes each group. The shared helper
 * binds the seat at v5, so this client binds its own at the top version. */

#include "wl_util.h"

static struct wl_seat* seat11;
static struct wl_pointer* ptr11;
static uint32_t seat_version;

static int enters;
static int axis_v, axis_h;
static int value120_v, value120_h;
static int reldir_v, reldir_h;
static int stop_v, stop_h;
static int source_wheel, source_finger, source_continuous;
static int frames;
static int first_v120;

static void p_enter(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s,
                    wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)serial; (void)s; (void)x; (void)y;
    enters++;
}
static void p_leave(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s) {
    (void)d; (void)p; (void)serial; (void)s;
}
static void p_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)t; (void)x; (void)y;
}
static void p_button(void* d, struct wl_pointer* p, uint32_t serial, uint32_t t,
                     uint32_t button, uint32_t state) {
    (void)d; (void)p; (void)serial; (void)t; (void)button; (void)state;
}
static void p_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t axis, wl_fixed_t value) {
    (void)d; (void)p; (void)t; (void)value;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) axis_v++;
    else axis_h++;
}
static void p_frame(void* d, struct wl_pointer* p) {
    (void)d; (void)p;
    frames++;
}
static void p_axis_source(void* d, struct wl_pointer* p, uint32_t source) {
    (void)d; (void)p;
    if (source == WL_POINTER_AXIS_SOURCE_WHEEL) source_wheel++;
    else if (source == WL_POINTER_AXIS_SOURCE_FINGER) source_finger++;
    else if (source == WL_POINTER_AXIS_SOURCE_CONTINUOUS) source_continuous++;
}
static void p_axis_stop(void* d, struct wl_pointer* p, uint32_t t, uint32_t axis) {
    (void)d; (void)p; (void)t;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) stop_v++;
    else stop_h++;
}
static void p_axis_discrete(void* d, struct wl_pointer* p, uint32_t axis, int32_t discrete) {
    (void)d; (void)p; (void)axis; (void)discrete;
}
static void p_axis_value120(void* d, struct wl_pointer* p, uint32_t axis, int32_t value120) {
    (void)d; (void)p;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        if (!value120_v) first_v120 = value120;
        value120_v++;
    } else {
        value120_h++;
    }
}
static void p_axis_relative_direction(void* d, struct wl_pointer* p, uint32_t axis,
                                      uint32_t direction) {
    (void)d; (void)p;
    if (direction != WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL) return;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) reldir_v++;
    else reldir_h++;
}

static const struct wl_pointer_listener ptr_listener = {
    p_enter, p_leave, p_motion, p_button, p_axis, p_frame, p_axis_source, p_axis_stop,
    p_axis_discrete, p_axis_value120, p_axis_relative_direction,
};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d;
    if (!strcmp(iface, wl_seat_interface.name) && !seat11) {
        seat_version = ver;
        seat11 = wl_registry_bind(r, name, &wl_seat_interface, ver > 11 ? 11 : ver);
    }
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!seat11 || seat_version < 9) {
        fprintf(stderr, "wl_seat at version %u, need 9 or more\n", seat_version);
        return 1;
    }

    ptr11 = wl_seat_get_pointer(seat11);
    wl_pointer_add_listener(ptr11, &ptr_listener, NULL);

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "scroll-axes", 400, 300, 0xFF20C020);
    wl_display_roundtrip(wl_dpy);
    printf("scroll axes ready\n");

    /* the scenario points at the window, then scrolls */
    for (int i = 0; i < 400 && !enters; i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(20000);
    }

    if (!enters) {
        fprintf(stderr, "the pointer never entered\n");
        return 1;
    }

    printf("entered\n");

    /* wheel notches on both axes, then a finger scroll, then the stops */
    for (int i = 0; i < 400 && !(stop_v && stop_h); i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(20000);
    }

    printf("axis v=%d h=%d value120 v=%d h=%d first_v=%d reldir v=%d h=%d stop v=%d h=%d "
           "source wheel=%d finger=%d continuous=%d frames=%d\n",
           axis_v, axis_h, value120_v, value120_h, first_v120, reldir_v, reldir_h,
           stop_v, stop_h, source_wheel, source_finger, source_continuous, frames);

    if (!axis_v || !axis_h || !value120_v || !value120_h || !reldir_v || !reldir_h ||
        !stop_v || !stop_h || !source_wheel || !source_finger || !frames) {
        fprintf(stderr, "a scroll event class never arrived\n");
        return 1;
    }

    printf("scroll axes ok\n");

    return 0;
}
