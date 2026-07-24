/* A solid window that then spins on frame callbacks: under the fake KMS the
 * compositor may only run as fast as the emulator's page-flip events, so
 * the spin measures the flip pacing. */
#include "wl_util.h"

static int frames;

static void frame_done(void* d, struct wl_callback* cb, uint32_t t);
static const struct wl_callback_listener frame_listener = {frame_done};

static struct wl_toplevel_ctx top;

static void frame_done(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d; (void)t;
    wl_callback_destroy(cb);
    frames++;

    struct wl_callback* next = wl_surface_frame(top.surface);
    wl_callback_add_listener(next, &frame_listener, NULL);
    wl_surface_commit(top.surface);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    wl_make_toplevel(&top, "kms-smoke", 300, 200, 0xff2060c0);
    printf("kms smoke mapped\n");

    struct wl_callback* cb = wl_surface_frame(top.surface);
    wl_callback_add_listener(cb, &frame_listener, NULL);
    wl_surface_commit(top.surface);

    while (wl_display_dispatch(wl_dpy) >= 0) {
    }

    return 0;
}
