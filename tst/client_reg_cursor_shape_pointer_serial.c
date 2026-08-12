// Regression: a cursor-shape device is tied to the wl_pointer passed to
// get_pointer. It must not accept an enter serial delivered to another
// wl_pointer object from the same seat/client.

#include "wl_util.h"
#include <cursor-shape-v1-client-protocol.h>
#include <linux/input-event-codes.h>

static struct wp_cursor_shape_manager_v1* shape_manager;
static struct wl_pointer* pointer2;
static uint32_t pointer2_enter_serial;

static void p2_enter(void* data, struct wl_pointer* pointer, uint32_t serial,
                     struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)pointer; (void)surface; (void)x; (void)y;
    pointer2_enter_serial = serial;
}
static void p2_leave(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* surface) {
    (void)d; (void)p; (void)s; (void)surface;
}
static void p2_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)t; (void)x; (void)y;
}
static void p2_button(void* d, struct wl_pointer* p, uint32_t s, uint32_t t,
                      uint32_t button, uint32_t state) {
    (void)d; (void)p; (void)s; (void)t; (void)button; (void)state;
}
static void p2_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t axis,
                    wl_fixed_t value) {
    (void)d; (void)p; (void)t; (void)axis; (void)value;
}
static void p2_frame(void* d, struct wl_pointer* p) { (void)d; (void)p; }
static void p2_axis_source(void* d, struct wl_pointer* p, uint32_t source) {
    (void)d; (void)p; (void)source;
}
static void p2_axis_stop(void* d, struct wl_pointer* p, uint32_t t, uint32_t axis) {
    (void)d; (void)p; (void)t; (void)axis;
}
static void p2_axis_discrete(void* d, struct wl_pointer* p, uint32_t axis, int32_t value) {
    (void)d; (void)p; (void)axis; (void)value;
}
static const struct wl_pointer_listener pointer2_listener = {
    .enter = p2_enter, .leave = p2_leave, .motion = p2_motion, .button = p2_button,
    .axis = p2_axis, .frame = p2_frame, .axis_source = p2_axis_source,
    .axis_stop = p2_axis_stop, .axis_discrete = p2_axis_discrete,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, wp_cursor_shape_manager_v1_interface.name))
        shape_manager = wl_registry_bind(registry, name,
                                         &wp_cursor_shape_manager_v1_interface, 1);
}
static void registry_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener registry_listener = {
    registry_global, registry_remove,
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

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!shape_manager) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "cursor-shape-pointer-serial", 360, 240, 0xFFFF0000);
    while (!wlp_enter_serial && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!wlp_enter_serial) return 1;

    uint32_t pointer1_serial = wlp_enter_serial;
    pointer2 = wl_seat_get_pointer(wl_seat_g);
    wl_pointer_add_listener(pointer2, &pointer2_listener, NULL);
    while (!pointer2_enter_serial && wl_display_roundtrip(wl_dpy) >= 0) {
    }
    if (!pointer2_enter_serial || pointer2_enter_serial == pointer1_serial) return 1;

    struct wp_cursor_shape_device_v1* device2 =
        wp_cursor_shape_manager_v1_get_pointer(shape_manager, pointer2);
    wl_display_roundtrip(wl_dpy);
    printf("cursor shape pointer serials ready\n");

    wait_key(KEY_1);
    wp_cursor_shape_device_v1_set_shape(device2, pointer1_serial,
                                        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
    wl_display_roundtrip(wl_dpy);
    printf("foreign shape serial sent\n");

    wait_key(KEY_2);
    wp_cursor_shape_device_v1_set_shape(device2, pointer2_enter_serial,
                                        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
    wl_display_roundtrip(wl_dpy);
    printf("own shape serial sent\n");

    wait_key(KEY_3);
    printf("cursor shape pointer serial ok\n");
    return 0;
}
