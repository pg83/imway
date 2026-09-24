// popup_done comes once a popup. A window grabs a popup off a key press;
// a child popup of it grabs off a serial one short of the press and is
// dismissed at once. Then the window unmaps, which dismisses the grabbing
// popup and every popup under it: the child, dismissed already, gets no
// second popup_done.

#include "wl_util.h"

struct popup_ctx {
    struct wl_surface* surface;
    struct xdg_surface* xs;
    struct xdg_popup* popup;
    int done, configured, committed;
};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)p;
    ((struct popup_ctx*)d)->done++;
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct popup_ctx* ctx = d;

    xdg_surface_ack_configure(xs, serial);
    ctx->configured = 1;
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void make_popup(struct popup_ctx* ctx, struct xdg_surface* parent, uint32_t serial) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

    xdg_positioner_set_size(pos, 80, 60);
    xdg_positioner_set_anchor_rect(pos, 10, 10, 20, 20);
    ctx->surface = wl_compositor_create_surface(wl_comp);
    ctx->xs = xdg_wm_base_get_xdg_surface(wl_wm, ctx->surface);
    xdg_surface_add_listener(ctx->xs, &popup_xs_listener, ctx);
    ctx->popup = xdg_surface_get_popup(ctx->xs, parent, pos);
    xdg_popup_add_listener(ctx->popup, &popup_listener, ctx);
    xdg_popup_grab(ctx->popup, wl_seat_g, serial);
    xdg_positioner_destroy(pos);
    wl_surface_commit(ctx->surface);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "popup-done-once", 300, 200, 0xff404040);
    printf("popup-done-once mapped\n");

    while (!wlk_press_serial && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct popup_ctx menu = {0}, child = {0};

    make_popup(&menu, top.xs, wlk_press_serial);
    while (!menu.configured && !menu.done && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (menu.done) {
        fprintf(stderr, "the key-serial grab was refused\n");
        return 1;
    }
    wl_surface_attach(menu.surface, wl_solid(80, 60, 0xffffff00), 0, 0);
    wl_surface_commit(menu.surface);
    wl_await_presented(menu.surface);

    make_popup(&child, menu.xs, wlk_press_serial - 1);
    while (!child.done && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("child dismissed at its grab\n");

    // unmapping the window dismisses its popup tree
    wl_surface_attach(top.surface, NULL, 0, 0);
    wl_surface_commit(top.surface);
    while (!menu.done && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);

    if (menu.done != 1 || child.done != 1) {
        fprintf(stderr, "popup_done counts: menu %d, child %d\n", menu.done, child.done);
        return 1;
    }

    printf("popup_done came once each\n");

    return 0;
}
