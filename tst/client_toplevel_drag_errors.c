// xdg-toplevel-drag misuse during a live drag, one mode per scenario:
//   ongoing   the drag object is destroyed while its drag runs (with a
//             toplevel attached): ONGOING_DRAG
//   attached  a second toplevel is attached while the first, mapped, still
//             is: TOPLEVEL_ATTACHED
// The drag starts from the scenario's button press on the red window.
//   usage: client_toplevel_drag_errors MODE

#include "wl_util.h"
#include <xdg-toplevel-drag-v1-client-protocol.h>

static struct xdg_toplevel_drag_manager_v1* drag_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_toplevel_drag_manager_v1_interface.name))
        drag_mgr = wl_registry_bind(r, name, &xdg_toplevel_drag_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (argc != 2) return 2;
    if (wl_boot() || !wl_ddm || !wl_ptr) return 2;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!drag_mgr) return 2;

    struct wl_toplevel_ctx origin, a, b;

    wl_make_toplevel(&origin, "drag-origin", 240, 160, 0xffff0000);

    struct wl_data_device* dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
    struct xdg_toplevel_drag_v1* drag = xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag(drag_mgr, src);

    wl_data_source_offer(src, "text/plain");
    printf("ready\n");
    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_data_device_start_drag(dev, src, origin.surface, NULL, wlp_button_serial);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "the drag did not start\n");
        return 1;
    }
    printf("dragging\n");
    // the windows the drag carries map once it runs, so nothing covers the
    // origin the scenario pressed on
    wl_make_toplevel(&a, "drag-a", 80, 60, 0xff00ff00);
    wl_make_toplevel(&b, "drag-b", 80, 60, 0xff0000ff);
    xdg_toplevel_drag_v1_attach(drag, a.tl, 5, 5);

    if (!strcmp(argv[1], "ongoing")) {
        xdg_toplevel_drag_v1_destroy(drag);
        if (wl_display_roundtrip(wl_dpy) >= 0) {
            fprintf(stderr, "destroying a live drag's object was accepted\n");
            return 1;
        }

        // the destructor took the proxy, so the error may name no interface
        const struct wl_interface* iface = NULL;
        uint32_t id = 0;
        uint32_t code = wl_display_get_protocol_error(wl_dpy, &iface, &id);

        if (wl_display_get_error(wl_dpy) != EPROTO || code != XDG_TOPLEVEL_DRAG_V1_ERROR_ONGOING_DRAG ||
            (iface && strcmp(iface->name, xdg_toplevel_drag_v1_interface.name))) {
            fprintf(stderr, "wrong error: %s code %u\n", iface ? iface->name : "?", code);
            return 1;
        }
        return 0;
    }

    if (!strcmp(argv[1], "attached")) {
        xdg_toplevel_drag_v1_attach(drag, b.tl, 5, 5);
        return wl_expect_error("xdg_toplevel_drag_v1", XDG_TOPLEVEL_DRAG_V1_ERROR_TOPLEVEL_ATTACHED) ? 1 : 0;
    }

    return 2;
}
