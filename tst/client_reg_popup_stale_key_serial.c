#include "wl_util.h"

static int popup_done_seen;
static void popup_configure(void* d, struct xdg_popup* popup, int32_t x, int32_t y,
                            int32_t w, int32_t h) {
    (void)d; (void)popup; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* popup) {
    (void)d; (void)popup; popup_done_seen = 1;
}
static void popup_repositioned(void* d, struct xdg_popup* popup, uint32_t token) {
    (void)d; (void)popup; (void)token;
}
static const struct xdg_popup_listener popup_listener = {
    popup_configure, popup_done, popup_repositioned,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(15);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx first, second;
    wl_make_toplevel(&first, "popup-stale-first", 200, 130, 0xffff0000);
    printf("first ready\n");
    while (!wlk_press_serial && wl_display_dispatch(wl_dpy) != -1) {}
    uint32_t stale = wlk_press_serial;

    wl_make_toplevel(&second, "popup-stale-second", 200, 130, 0xff00ff00);
    if (wlk_focus != second.surface) return 1;
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 60, 40);
    xdg_positioner_set_anchor_rect(pos, 0, 0, 10, 10);
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    struct xdg_popup* popup = xdg_surface_get_popup(xs, second.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_popup_grab(popup, wl_seat_g, stale);
    wl_surface_commit(surface);
    while (!popup_done_seen && wl_display_dispatch(wl_dpy) != -1) {}
    printf("stale popup dismissed\n");
    return popup_done_seen ? 0 : 1;
}
