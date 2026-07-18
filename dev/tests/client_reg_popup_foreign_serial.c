#include "wl_util.h"

static int dismissed;
static void pop_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y,
                          int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void pop_done(void* d, struct xdg_popup* p) { (void)d; (void)p; dismissed = 1; }
static void pop_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener pop_listener = {
    pop_configure, pop_done, pop_repositioned,
};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    if (argc < 2 || !strcmp(argv[1], "owner")) {
        wl_make_toplevel(&top, "popup-serial-owner", 200, 130, 0xffff0000);
        printf("owner ready\n");
        while (!wlk_press_serial && wl_display_dispatch(wl_dpy) != -1) {}
        printf("popup foreign serial %u\n", wlk_press_serial);
        while (wl_display_dispatch(wl_dpy) != -1) {}
        return 0;
    }

    uint32_t serial = (uint32_t)strtoul(argv[1], NULL, 10);
    wl_make_toplevel(&top, "popup-serial-attacker", 200, 130, 0xff00ff00);
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 60, 40);
    xdg_positioner_set_anchor_rect(pos, 0, 0, 10, 10);
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    struct xdg_popup* popup = xdg_surface_get_popup(xs, top.xs, pos);
    xdg_popup_add_listener(popup, &pop_listener, NULL);
    xdg_popup_grab(popup, wl_seat_g, serial);
    wl_surface_commit(surface);
    while (!dismissed && wl_display_dispatch(wl_dpy) != -1) {}
    printf("foreign popup dismissed\n");
    return dismissed ? 0 : 1;
}
