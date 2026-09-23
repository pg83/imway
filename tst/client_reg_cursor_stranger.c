// A client the pointer is not over cannot set the cursor. Mode "owner" maps a
// green window and idles; mode "stranger" maps a red one, waits for the
// pointer to enter it and then to leave it (for the owner's window), and
// then asks for a cursor surface with wl_pointer.set_cursor and for a shape
// through wp_cursor_shape_device_v1, both on the enter serial it had. The
// scenario checks that neither took. Both exit on the go-exit file.

#include "wl_util.h"
#include <cursor-shape-v1-client-protocol.h>

static struct wp_cursor_shape_manager_v1* shape_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
        shape_mgr = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void wait_go(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);
    while (access(path, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) exit(1);
        usleep(20000);
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (argc < 2 || wl_boot() || !wl_ptr) return 2;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!shape_mgr) return 2;

    struct wl_toplevel_ctx top;

    if (!strcmp(argv[1], "owner")) {
        wl_make_toplevel(&top, "cursor-owner", 200, 150, 0xFF00FF00);
        printf("owner ready\n");
        wait_go("exit");
        return 0;
    }

    struct wp_cursor_shape_device_v1* device = wp_cursor_shape_manager_v1_get_pointer(shape_mgr, wl_ptr);

    wl_make_toplevel(&top, "cursor-stranger", 200, 150, 0xFFFF0000);
    printf("stranger ready\n");
    while (wlp_focus != top.surface && wl_display_dispatch(wl_dpy) != -1) {
    }
    uint32_t serial = wlp_enter_serial;
    printf("entered\n");
    while (wlp_focus && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("left\n");

    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    wl_surface_attach(cursor, wl_solid(16, 16, 0xFF0000FF), 0, 0);
    wl_surface_commit(cursor);
    wl_pointer_set_cursor(wl_ptr, serial, cursor, 0, 0);
    wp_cursor_shape_device_v1_set_shape(device, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    printf("asked\n");
    wait_go("exit");
    return 0;
}
