// wp-cursor-shape v2: the compositor must advertise version >= 2 and accept
// the shapes added in v2 (zoom_in/out, dnd_ask, all_resize) without a
// protocol error.

#include "wl_util.h"
#include <cursor-shape-v1-client-protocol.h>

static struct wp_cursor_shape_manager_v1* shape_mgr;
static uint32_t shape_version;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name)) {
        shape_version = v;
        shape_mgr = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, v < 2 ? v : 2);
    }
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!shape_mgr) {
        fprintf(stderr, "no cursor_shape_manager\n");
        return 1;
    }
    if (shape_version < 2) {
        fprintf(stderr, "cursor_shape_manager at version %u, want >= 2\n", shape_version);
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "cshape2", 200, 150, 0xFF6080A0u);

    struct wp_cursor_shape_device_v1* dev =
        wp_cursor_shape_manager_v1_get_pointer(shape_mgr, wl_ptr);

    // move the pointer over the surface so we have a pointer focus + serial
    for (int i = 0; i < 100 && !wlp_enter_count; i++) {
        if (wl_display_dispatch(wl_dpy) < 0) break;
    }
    if (!wlp_enter_count) {
        fprintf(stderr, "pointer never entered\n");
        return 1;
    }

    // a v2-only shape must be accepted, not a protocol error
    wp_cursor_shape_device_v1_set_shape(dev, wlp_enter_serial,
                                        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_RESIZE);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "all_resize shape raised a protocol error\n");
        return 1;
    }

    printf("client_reg_cursor_shape_version_2: v2 shape accepted\n");
    return 0;
}
