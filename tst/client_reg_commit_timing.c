#include "wl_util.h"

#include <commit-timing-v1-client-protocol.h>
#include <time.h>

// #C-8: wp-commit-timing. A commit carrying a target timestamp 300ms out
// must not be presented before that time. The frame callback's timestamp
// rides the compositor's CLOCK_MONOTONIC, the same clock the timestamp is
// expressed in, so the assertion is a plain lower bound with no scheduling
// sensitivity. Without the protocol the commit applies immediately.

static struct wp_commit_timing_manager_v1* timing_mgr;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_commit_timing_manager_v1_interface.name))
        timing_mgr = wl_registry_bind(registry, name,
                                      &wp_commit_timing_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static uint32_t done_ms;
static int done_seen;

static void frame_done(void* d, struct wl_callback* cb, uint32_t ms) {
    (void)d;
    done_ms = ms;
    done_seen = 1;
    wl_callback_destroy(cb);
}
static const struct wl_callback_listener frame_listener = {frame_done};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!timing_mgr) {
        fprintf(stderr, "no wp_commit_timing_manager_v1\n");
        return 2;
    }

    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "commit-timing", 300, 300, 0xffff0000);

    struct wp_commit_timer_v1* timer =
        wp_commit_timing_manager_v1_get_timer(timing_mgr, ctx.surface);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint64_t target_ns = (uint64_t)now.tv_sec * 1000000000ull +
                         (uint64_t)now.tv_nsec + 300000000ull;
    uint64_t sec = target_ns / 1000000000ull;
    uint32_t nsec = (uint32_t)(target_ns % 1000000000ull);

    wp_commit_timer_v1_set_timestamp(timer, (uint32_t)(sec >> 32),
                                     (uint32_t)sec, nsec);
    wl_surface_attach(ctx.surface, wl_solid(300, 300, 0xff00ff00), 0, 0);
    wl_surface_damage(ctx.surface, 0, 0, 300, 300);
    struct wl_callback* cb = wl_surface_frame(ctx.surface);
    wl_callback_add_listener(cb, &frame_listener, NULL);
    wl_surface_commit(ctx.surface);

    while (!done_seen && wl_display_dispatch(wl_dpy) != -1) {
    }

    uint32_t target_ms = (uint32_t)(target_ns / 1000000ull);

    // presented-at-or-after: one frame of slack below the target only
    if ((int32_t)(done_ms - target_ms) < -20) {
        fprintf(stderr, "timed commit presented at %u ms, target %u ms: applied early\n",
                done_ms, target_ms);
        return 1;
    }

    printf("timing done\n");
    fflush(stdout);
    return 0;
}
