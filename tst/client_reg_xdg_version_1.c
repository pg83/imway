#define REG_XDG_VERSION 1
#include "wl_util.h"

static int repositioned;
static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y,
                            int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token; repositioned++;
}
static const struct xdg_popup_listener popup_listener = {
    popup_configure, popup_done, popup_repositioned,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    if (wl_boot() || xdg_wm_base_get_version(wl_wm) != 1) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "xdg-version-1", 200, 130, 0xffff0000);

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 60, 40);
    xdg_positioner_set_anchor_rect(pos, 0, 0, 10, 10);
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    struct xdg_popup* popup = xdg_surface_get_popup(xs, top.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    wl_surface_commit(surface);
    if (wl_display_roundtrip(wl_dpy) < 0 ||
        wl_display_roundtrip(wl_dpy) < 0) return 1;

    printf("xdg-v1 popup-version=%u positioner-version=%u repositioned=%d\n",
           xdg_popup_get_version(popup), xdg_positioner_get_version(pos), repositioned);
    return xdg_popup_get_version(popup) == 1 &&
           xdg_positioner_get_version(pos) == 1 && !repositioned ? 0 : 1;
}
