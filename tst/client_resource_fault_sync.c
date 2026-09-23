// The linux-drm-syncobj objects and a tablet tool's cursor-shape device
// failing to allocate: the scenario's IMWAY_CHAOS arms the fault, the client
// runs the chain that reaches it and must get wl_display.no_memory. Exits 77
// without the syncobj global (explicit sync unavailable).
//   usage: client_resource_fault_sync sync-bind|sync-surface|sync-timeline|cursor-tablet
#include "syncobj_error.inc"

#include <cursor-shape-v1-client-protocol.h>
#include <tablet-v2-client-protocol.h>

static struct wp_cursor_shape_manager_v1* shapes;
static struct zwp_tablet_manager_v2* tablets;
static struct zwp_tablet_tool_v2* tool;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
        shapes = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_tablet_manager_v2_interface.name))
        tablets = wl_registry_bind(r, name, &zwp_tablet_manager_v2_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void seat_tablet(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_v2* t) { (void)d; (void)s; (void)t; }
static void seat_tool(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_tool_v2* t) { (void)d; (void)s; tool = t; }
static void seat_pad(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_pad_v2* p) { (void)d; (void)s; (void)p; }
static const struct zwp_tablet_seat_v2_listener seat_listener = {seat_tablet, seat_tool, seat_pad};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (argc != 2) return 2;

    const char* mode = argv[1];

    if (!strcmp(mode, "cursor-tablet")) {
        if (wl_boot()) return 2;

        struct wl_registry* reg = wl_display_get_registry(wl_dpy);

        wl_registry_add_listener(reg, &extra_listener, NULL);
        wl_display_roundtrip(wl_dpy);
        if (!shapes || !tablets) return 2;

        struct zwp_tablet_seat_v2* ts = zwp_tablet_manager_v2_get_tablet_seat(tablets, wl_seat_g);

        zwp_tablet_seat_v2_add_listener(ts, &seat_listener, NULL);
        wl_display_roundtrip(wl_dpy);
        if (!tool) return 2;
        wp_cursor_shape_manager_v1_get_tablet_tool_v2(shapes, tool);
        return wl_expect_error("wl_display", WL_DISPLAY_ERROR_NO_MEMORY);
    }

    // the manager's bind is the one that fails in sync-bind: the boot's
    // roundtrip is where the no_memory arrives
    int rc = sync_test_boot();

    if (!strcmp(mode, "sync-bind")) {
        if (rc == 77) return 77;

        const struct wl_interface* iface = NULL;
        uint32_t id = 0;
        uint32_t code = wl_display_get_protocol_error(wl_dpy, &iface, &id);

        if (wl_display_roundtrip(wl_dpy) >= 0 || wl_display_get_error(wl_dpy) != ENOMEM || code != WL_DISPLAY_ERROR_NO_MEMORY) {
            fprintf(stderr, "the failed syncobj manager bind was not no_memory\n");
            return 1;
        }
        return 0;
    }
    if (rc) return rc;

    if (!strcmp(mode, "sync-surface")) {
        wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, wl_compositor_create_surface(wl_comp));
    } else if (!strcmp(mode, "sync-timeline")) {
        if (!sync_test_timeline()) return 77;
    } else {
        return 2;
    }
    return wl_expect_error("wl_display", WL_DISPLAY_ERROR_NO_MEMORY);
}
