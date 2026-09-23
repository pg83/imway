// xdg-toplevel-drag objects whose drag never starts, one mode per run:
//   selection  a source already holding the clipboard cannot carry a
//              toplevel drag: INVALID_SOURCE
//   idle       attaching before any drag is bookkeeping only: the same
//              toplevel again is fine, and so is another one once the
//              first unmapped; the object may go after its source did
//   usage: client_reg_toplevel_drag_idle MODE

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
    if (wl_boot() || !wl_ddm) return 2;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!drag_mgr) return 2;

    struct wl_toplevel_ctx a, b;

    wl_make_toplevel(&a, "drag-idle-a", 80, 60, 0xff00ff00);

    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);

    wl_data_source_offer(src, "text/plain");

    if (!strcmp(argv[1], "selection")) {
        struct wl_data_device* dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);

        while (!wlk_enters && wl_display_dispatch(wl_dpy) != -1) {
        }
        wl_data_device_set_selection(dev, src, wlk_enter_serial);
        xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag(drag_mgr, src);
        return wl_expect_error(xdg_toplevel_drag_manager_v1_interface.name, XDG_TOPLEVEL_DRAG_MANAGER_V1_ERROR_INVALID_SOURCE);
    }

    if (!strcmp(argv[1], "idle")) {
        struct xdg_toplevel_drag_v1* drag = xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag(drag_mgr, src);

        wl_make_toplevel(&b, "drag-idle-b", 80, 60, 0xff0000ff);
        xdg_toplevel_drag_v1_attach(drag, a.tl, 5, 5);
        xdg_toplevel_drag_v1_attach(drag, a.tl, 6, 6);
        wl_surface_attach(a.surface, NULL, 0, 0);
        wl_surface_commit(a.surface);
        xdg_toplevel_drag_v1_attach(drag, b.tl, 5, 5);
        wl_data_source_destroy(src);
        xdg_toplevel_drag_v1_destroy(drag);
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "an idle toplevel drag was refused (error %d)\n", wl_display_get_error(wl_dpy));
            return 1;
        }
        printf("idle drag ok\n");
        return 0;
    }

    return 2;
}
