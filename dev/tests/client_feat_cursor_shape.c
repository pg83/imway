// Feature: cursor-shape-v1. A focused client may name its cursor by shape via
// wp_cursor_shape_device_v1.set_shape; the request must be accepted (no
// protocol error) and leave the compositor running.

#include "wl_util.h"
#include <cursor-shape-v1-client-protocol.h>

static struct wp_cursor_shape_manager_v1* shape_mgr;
static struct wp_cursor_shape_device_v1* shape_dev;
static struct wl_toplevel_ctx top;

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
        shape_mgr = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ptr) { fprintf(stderr, "no pointer\n"); return 1; }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!shape_mgr) { fprintf(stderr, "no cursor-shape manager\n"); return 1; }
    shape_dev = wp_cursor_shape_manager_v1_get_pointer(shape_mgr, wl_ptr);

    wl_make_toplevel(&top, "client_feat_cursor_shape", 400, 300, 0xFFFF0000);
    printf("client_feat_cursor_shape: mapped\n");

    // wait for pointer focus (the scenario points at us), then name a shape
    for (int i = 0; i < 300 && !wlp_enter_serial; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!wlp_enter_serial) { fprintf(stderr, "pointer never entered\n"); return 1; }

    wp_cursor_shape_device_v1_set_shape(shape_dev, wlp_enter_serial,
                                        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
    if (wl_display_roundtrip(wl_dpy) < 0) { fprintf(stderr, "set_shape errored\n"); return 1; }

    printf("client_feat_cursor_shape: shape set\n");
    printf("client_feat_cursor_shape: ok\n");
    return 0;
}
