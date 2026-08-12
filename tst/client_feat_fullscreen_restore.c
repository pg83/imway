// Feature: fullscreen and back. KEY_F requests fullscreen, KEY_U leaves it;
// the configure after unset must restore the pre-fullscreen size. The
// client answers every configure with a matching buffer and reports states.

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int cur_w = 300, cur_h = 200;
static int pend_w, pend_h, pend_fs;
static int mapped_printed;

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t;
    pend_w = w;
    pend_h = h;
    pend_fs = 0;
    uint32_t* s;
    wl_array_for_each(s, states) {
        if (*s == XDG_TOPLEVEL_STATE_FULLSCREEN) pend_fs = 1;
    }
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (pend_w > 0) cur_w = pend_w;
    if (pend_h > 0) cur_h = pend_h;
    wl_surface_attach(surface, wl_solid(cur_w, cur_h, 0xFFFF0000), 0, 0);
    wl_surface_damage(surface, 0, 0, cur_w, cur_h);
    wl_surface_commit(surface);
    printf("configured %dx%d fs=%d\n", cur_w, cur_h, pend_fs);
    if (!mapped_printed) {
        printf("fs client mapped\n");
        mapped_printed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "fsrestore");
    xdg_toplevel_set_app_id(tl, "fsrestore");
    wl_surface_commit(surface);

    wlk_watch_key = KEY_F;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    xdg_toplevel_set_fullscreen(tl, NULL);
    wl_display_flush(wl_dpy);

    wlk_watch_key = KEY_U;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    xdg_toplevel_unset_fullscreen(tl);
    wl_display_flush(wl_dpy);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
