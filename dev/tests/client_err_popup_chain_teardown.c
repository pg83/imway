/* PLAN depth #6: a popup chain of reasonable depth, each popup mapped before
 * its child is created (the compositor rejects a popup whose parent is not
 * mapped at its initial commit). Destroying a middle popup while it still has
 * a live child must raise NOT_THE_TOPMOST_POPUP and leave no grab/focus state
 * behind (the scenario checks the dump afterwards). */
#include "wl_util.h"

#define CHAIN 6

struct pop {
    struct wl_surface* surface;
    struct xdg_surface* xs;
    struct xdg_popup* popup;
    int mapped;
};

static struct pop pops[CHAIN];

static struct xdg_positioner* make_pos(void) {
    struct xdg_positioner* p = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(p, 20, 20);
    xdg_positioner_set_anchor_rect(p, 0, 0, 10, 10);
    return p;
}

static void pop_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct pop* pp = d;
    xdg_surface_ack_configure(xs, serial);
    if (!pp->mapped) {
        wl_surface_attach(pp->surface, wl_solid(20, 20, 0xff00ffff), 0, 0);
        wl_surface_damage(pp->surface, 0, 0, 20, 20);
        wl_surface_commit(pp->surface);
        pp->mapped = 1;
    }
}
static const struct xdg_surface_listener pop_xs_listener = {pop_xs_configure};

static void pop_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                          int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void pop_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static const struct xdg_popup_listener pop_listener = {pop_configure, pop_done};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "popup-chain", 128, 128, 0xffff0000);

    struct xdg_surface* parent_xs = top.xs;
    for (int i = 0; i < CHAIN; i++) {
        pops[i].surface = wl_compositor_create_surface(wl_comp);
        pops[i].xs = xdg_wm_base_get_xdg_surface(wl_wm, pops[i].surface);
        xdg_surface_add_listener(pops[i].xs, &pop_xs_listener, &pops[i]);
        pops[i].popup = xdg_surface_get_popup(pops[i].xs, parent_xs, make_pos());
        xdg_popup_add_listener(pops[i].popup, &pop_listener, NULL);
        wl_surface_commit(pops[i].surface); /* initial commit solicits configure */

        /* map this popup before its child commits (parent must be mapped) */
        while (!pops[i].mapped && wl_display_dispatch(wl_dpy) != -1) {
        }
        if (!pops[i].mapped || wl_display_roundtrip(wl_dpy) < 0) return 2;
        parent_xs = pops[i].xs;
    }

    /* destroy a middle popup while it still has a live child */
    xdg_popup_destroy(pops[CHAIN / 2].popup);

    return wl_expect_error(xdg_wm_base_interface.name, XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP);
}
