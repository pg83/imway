// Regression: wl_surface v4 requires the buffer size to be divisible by
// buffer_scale; committing a 101x100 buffer at scale 2 must be answered
// with the invalid_size protocol error, not silently accepted.

#include "wl_util.h"
#include <errno.h>

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int configured;

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    configured = 1;
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* s) {
    (void)d; (void)t; (void)w; (void)h; (void)s;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_app_id(tl, "scale-size");
    wl_surface_commit(surface);
    while (!configured && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_surface_set_buffer_scale(surface, 2);
    wl_surface_attach(surface, wl_solid(101, 100, 0xFFFF0000), 0, 0);
    wl_surface_damage(surface, 0, 0, 101, 100);
    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    const struct wl_interface* iface = NULL;
    uint32_t id;
    int err = wl_display_get_error(wl_dpy);
    if (err != EPROTO) {
        fprintf(stderr, "expected a protocol error, got errno %d\n", err);
        return 1;
    }
    uint32_t code = wl_display_get_protocol_error(wl_dpy, &iface, &id);
    if (!iface || strcmp(iface->name, wl_surface_interface.name) ||
        code != WL_SURFACE_ERROR_INVALID_SIZE) {
        fprintf(stderr, "wrong error: %s code %u\n", iface ? iface->name : "?", code);
        return 1;
    }
    printf("client_reg_scale_size: invalid_size delivered\n");
    return 0;
}
