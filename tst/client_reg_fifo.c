#include "wl_util.h"

#include <fifo-v1-client-protocol.h>

// #C-7: wp-fifo. Queue several content updates, each carrying set_barrier +
// wait_barrier and its own frame callback. FIFO semantics mean the compositor
// applies at most one queued update per presentation, so the callbacks must
// come back with strictly increasing presentation timestamps. Without fifo
// every commit applies immediately and all callbacks fire on one frame with
// one timestamp.

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

int main(void) {
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
    wl_make_toplevel(&ctx, "fifo", 300, 300, 0xffff0000);

    // let the initial red frame reach the screen so the frame clock is live
    struct wl_callback* settle = wl_surface_frame(ctx.surface);
    wl_callback_add_listener(settle, &settle_listener, NULL);
    wl_surface_commit(ctx.surface);
    while (!settle_done && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wp_fifo_v1* fifo = wp_fifo_manager_v1_get_fifo(fifo_mgr, ctx.surface);
    static const uint32_t colors[QUEUED] = {
        0xff00ff00, 0xff0000ff, 0xffffff00, 0xffff00ff,
    };

    // all four commits go out back to back: the first applies immediately
    // (no barrier is up yet) and raises one, the rest queue behind it
    for (int i = 0; i < QUEUED; i++) {
        wl_surface_attach(ctx.surface, wl_solid(300, 300, colors[i]), 0, 0);
        wl_surface_damage(ctx.surface, 0, 0, 300, 300);
        struct wl_callback* cb = wl_surface_frame(ctx.surface);
        wl_callback_add_listener(cb, &frame_listener, NULL);
        wp_fifo_v1_set_barrier(fifo);
        wp_fifo_v1_wait_barrier(fifo);
        wl_surface_commit(ctx.surface);
    }
    wl_display_flush(wl_dpy);

    while (done_n < QUEUED && wl_display_dispatch(wl_dpy) != -1) {
    }

    for (int i = 1; i < QUEUED; i++) {
        if (done_ms[i] <= done_ms[i - 1]) {
            fprintf(stderr,
                    "updates %d and %d presented in the same frame (%u <= %u): fifo did not queue\n",
                    i - 1, i, done_ms[i], done_ms[i - 1]);
            return 1;
        }
    }

    printf("fifo done\n");
    fflush(stdout);

    // stay mapped for the scenario's final screenshot
    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
