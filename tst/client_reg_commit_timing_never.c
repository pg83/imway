#include "wl_util.h"
#include <commit-timing-v1-client-protocol.h>

// wp-commit-timing with a target time past what nanoseconds in 64 bits can
// hold: the compositor saturates it to "never" instead of wrapping it round
// to some time already past. The green commit stays held, so its frame
// callback does not come while the window keeps presenting its red frames.

static struct wp_commit_timing_manager_v1* timing_mgr;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name, const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_commit_timing_manager_v1_interface.name))
        timing_mgr = wl_registry_bind(registry, name, &wp_commit_timing_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int done_seen;

static void frame_done(void* d, struct wl_callback* cb, uint32_t ms) {
    (void)d; (void)ms;
    done_seen = 1;
    wl_callback_destroy(cb);
}
static const struct wl_callback_listener frame_listener = {frame_done};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!timing_mgr) return 2;

    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "commit-timing-never", 300, 300, 0xffff0000);

    struct wp_commit_timer_v1* timer = wp_commit_timing_manager_v1_get_timer(timing_mgr, ctx.surface);

    // 2^55 seconds: times 10^9 (2^9 * 1953125) that is a multiple of 2^64,
    // so a wrapping product would read as time zero, long past
    wp_commit_timer_v1_set_timestamp(timer, 0x00800000u, 0, 0);
    wl_surface_attach(ctx.surface, wl_solid(300, 300, 0xff00ff00), 0, 0);
    wl_surface_damage(ctx.surface, 0, 0, 300, 300);
    struct wl_callback* cb = wl_surface_frame(ctx.surface);
    wl_callback_add_listener(cb, &frame_listener, NULL);
    wl_surface_commit(ctx.surface);
    wl_display_roundtrip(wl_dpy);
    printf("committed\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/go-exit", getenv("XDG_RUNTIME_DIR"));
    while (access(path, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }
    if (done_seen) {
        fprintf(stderr, "a commit timed for never was presented\n");
        return 1;
    }
    printf("still held\n");
    return 0;
}
