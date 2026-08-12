// Regression: wl_pointer.set_cursor serials belong to the wl_pointer object
// that received the enter. A second pointer must reject the first pointer's
// enter serial and accept its own.

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_pointer* pointer2;
static uint32_t pointer2_enter_serial;

static void p2_enter(void* data, struct wl_pointer* pointer, uint32_t serial,
                     struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)pointer; (void)surface; (void)x; (void)y;
    pointer2_enter_serial = serial;
}
static void p2_leave(void* data, struct wl_pointer* pointer, uint32_t serial,
                     struct wl_surface* surface) {
    (void)data; (void)pointer; (void)serial; (void)surface;
}
static void p2_motion(void* data, struct wl_pointer* pointer, uint32_t time,
                      wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)pointer; (void)time; (void)x; (void)y;
}
static void p2_button(void* data, struct wl_pointer* pointer, uint32_t serial,
                      uint32_t time, uint32_t button, uint32_t state) {
    (void)data; (void)pointer; (void)serial; (void)time; (void)button; (void)state;
}
static void p2_axis(void* data, struct wl_pointer* pointer, uint32_t time,
                    uint32_t axis, wl_fixed_t value) {
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}
static void p2_frame(void* data, struct wl_pointer* pointer) {
    (void)data; (void)pointer;
}
static void p2_axis_source(void* data, struct wl_pointer* pointer, uint32_t source) {
    (void)data; (void)pointer; (void)source;
}
static void p2_axis_stop(void* data, struct wl_pointer* pointer, uint32_t time,
                         uint32_t axis) {
    (void)data; (void)pointer; (void)time; (void)axis;
}
static void p2_axis_discrete(void* data, struct wl_pointer* pointer, uint32_t axis,
                             int32_t discrete) {
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointer2_listener = {
    .enter = p2_enter,
    .leave = p2_leave,
    .motion = p2_motion,
    .button = p2_button,
    .axis = p2_axis,
    .frame = p2_frame,
    .axis_source = p2_axis_source,
    .axis_stop = p2_axis_stop,
    .axis_discrete = p2_axis_discrete,
};

static void wait_key(uint32_t key) {
    wlk_watch_key = key;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot() || !wl_ptr || !wl_seat_g) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "cursor-pointer-serial", 360, 240, 0xFFFF0000);

    while (!wlp_enter_serial && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!wlp_enter_serial) return 1;

    uint32_t pointer1_serial = wlp_enter_serial;
    pointer2 = wl_seat_get_pointer(wl_seat_g);
    wl_pointer_add_listener(pointer2, &pointer2_listener, NULL);
    while (!pointer2_enter_serial && wl_display_roundtrip(wl_dpy) >= 0) {
    }
    if (!pointer2_enter_serial || pointer2_enter_serial == pointer1_serial) {
        fprintf(stderr, "second pointer did not receive a distinct enter serial\n");
        return 1;
    }

    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    wl_surface_attach(cursor, wl_solid(12, 12, 0xFFFFFFFF), 0, 0);
    wl_surface_damage(cursor, 0, 0, 12, 12);
    wl_surface_commit(cursor);
    printf("cursor pointer serials ready\n");

    wait_key(KEY_1);
    wl_pointer_set_cursor(pointer2, pointer1_serial, cursor, 0, 0);
    wl_display_roundtrip(wl_dpy);
    printf("foreign pointer serial sent\n");

    wait_key(KEY_2);
    wl_pointer_set_cursor(pointer2, pointer2_enter_serial, cursor, 0, 0);
    wl_display_roundtrip(wl_dpy);
    printf("own pointer serial sent\n");

    wait_key(KEY_3);
    printf("cursor pointer serial ok\n");
    return 0;
}
