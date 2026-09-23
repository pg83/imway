// A cursor-shape device for a tablet tool, and a wl_touch. The tool's shape
// device has no seat pointer behind it: a shape set on it changes nothing
// and is no error, and it goes away quietly. The touch object is handed
// out and released like any other seat device.

#include "wl_util.h"
#include <cursor-shape-v1-client-protocol.h>
#include <tablet-v2-client-protocol.h>

static struct zwp_tablet_manager_v2* tablet_mgr;
static struct wp_cursor_shape_manager_v1* shapes;
static struct zwp_tablet_tool_v2* tool;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_tablet_manager_v2_interface.name))
        tablet_mgr = wl_registry_bind(r, name, &zwp_tablet_manager_v2_interface, 1);
    else if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
        shapes = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void seat_tablet_added(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_v2* t) {
    (void)d; (void)s; (void)t;
}
static void seat_tool_added(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_tool_v2* t) {
    (void)d; (void)s;
    if (!tool)
        tool = t;
}
static void seat_pad_added(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_pad_v2* p) {
    (void)d; (void)s; (void)p;
}
static const struct zwp_tablet_seat_v2_listener tablet_seat_listener = {
    seat_tablet_added, seat_tool_added, seat_pad_added,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);

    if (wl_boot()) return 2;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!tablet_mgr || !shapes || !wl_seat_g) return 2;

    struct zwp_tablet_seat_v2* tseat = zwp_tablet_manager_v2_get_tablet_seat(tablet_mgr, wl_seat_g);

    zwp_tablet_seat_v2_add_listener(tseat, &tablet_seat_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (!tool) {
        fprintf(stderr, "no tablet tool announced\n");
        return 1;
    }

    struct wp_cursor_shape_device_v1* device = wp_cursor_shape_manager_v1_get_tablet_tool_v2(shapes, tool);

    wp_cursor_shape_device_v1_set_shape(device, 1, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR);
    wp_cursor_shape_device_v1_destroy(device);

    struct wl_touch* touch = wl_seat_get_touch(wl_seat_g);

    wl_touch_release(touch);

    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "the tool's shape device or the touch object was refused\n");
        return 1;
    }

    printf("tablet tool cursor done\n");

    return 0;
}
