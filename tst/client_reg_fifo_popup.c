#include "wl_util.h"

#include <fifo-v1-client-protocol.h>

// wp-fifo on a popup: a mapped popup is presented like its toplevel, so its
// queued updates also apply one per presentation, and their frame callbacks
// come back with strictly increasing timestamps. Were the popup taken for
// invisible, its barrier would not hold and every update would land in one
// frame.

static struct wp_fifo_manager_v1* fifo_mgr;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_fifo_manager_v1_interface.name))
        fifo_mgr = wl_registry_bind(registry, name, &wp_fifo_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

#define QUEUED 4
#define PW 120
#define PH 90

static uint32_t done_ms[QUEUED];
static int done_n = 0;

static void frame_done(void* d, struct wl_callback* cb, uint32_t ms) {
    (void)d;
    if (done_n < QUEUED)
        done_ms[done_n] = ms;
    done_n++;
    wl_callback_destroy(cb);
}
static const struct wl_callback_listener frame_listener = {frame_done};

static int settle_done;
static void settle_cb(void* d, struct wl_callback* cb, uint32_t ms) {
    (void)d; (void)ms;
    settle_done = 1;
    wl_callback_destroy(cb);
}
static const struct wl_callback_listener settle_listener = {settle_cb};

static struct wl_surface* popup_surface;
static int popup_committed;

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!popup_committed) {
        wl_surface_attach(popup_surface, wl_solid(PW, PH, 0xffffffff), 0, 0);
        wl_surface_commit(popup_surface);
        popup_committed = 1;
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!fifo_mgr) {
        fprintf(stderr, "no wp_fifo_manager_v1\n");
        return 2;
    }

    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "fifo-popup", 300, 300, 0xffff0000);

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

    xdg_positioner_set_size(pos, PW, PH);
    xdg_positioner_set_anchor_rect(pos, 40, 40, 1, 1);
    popup_surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* popup_xs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
    xdg_surface_add_listener(popup_xs, &popup_xs_listener, NULL);
    struct xdg_popup* popup = xdg_surface_get_popup(popup_xs, ctx.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_positioner_destroy(pos);
    wl_surface_commit(popup_surface);

    while (!popup_committed && wl_display_dispatch(wl_dpy) != -1) {
    }

    // let the popup's first frame reach the screen so its frame clock is live
    struct wl_callback* settle = wl_surface_frame(popup_surface);
    wl_callback_add_listener(settle, &settle_listener, NULL);
    wl_surface_commit(popup_surface);
    while (!settle_done && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wp_fifo_v1* fifo = wp_fifo_manager_v1_get_fifo(fifo_mgr, popup_surface);
    static const uint32_t colors[QUEUED] = {
        0xff00ff00, 0xff0000ff, 0xffffff00, 0xffff00ff,
    };

    for (int i = 0; i < QUEUED; i++) {
        wl_surface_attach(popup_surface, wl_solid(PW, PH, colors[i]), 0, 0);
        wl_surface_damage(popup_surface, 0, 0, PW, PH);
        struct wl_callback* cb = wl_surface_frame(popup_surface);
        wl_callback_add_listener(cb, &frame_listener, NULL);
        wp_fifo_v1_set_barrier(fifo);
        wp_fifo_v1_wait_barrier(fifo);
        wl_surface_commit(popup_surface);
    }
    wl_display_flush(wl_dpy);

    while (done_n < QUEUED && wl_display_dispatch(wl_dpy) != -1) {
    }

    for (int i = 1; i < QUEUED; i++) {
        if (done_ms[i] <= done_ms[i - 1]) {
            fprintf(stderr,
                    "popup updates %d and %d presented in the same frame (%u <= %u): fifo did not queue\n",
                    i - 1, i, done_ms[i], done_ms[i - 1]);
            return 1;
        }
    }

    printf("fifo popup done\n");

    wp_fifo_v1_destroy(fifo);
    xdg_popup_destroy(popup);
    xdg_surface_destroy(popup_xs);
    wl_surface_destroy(popup_surface);
    wl_display_roundtrip(wl_dpy);
    return 0;
}
