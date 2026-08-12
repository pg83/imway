// Regression: a gesture sequence belongs to the client selected at begin.
// Losing pointer focus mid-swipe must not orphan the sequence; update/end still
// go to the gesture objects that received begin.

#include "wl_util.h"
#include <pointer-gestures-unstable-v1-client-protocol.h>

static struct zwp_pointer_gestures_v1* gestures;
static int begins;
static int updates;
static int ends;

static void swipe_begin(void* data, struct zwp_pointer_gesture_swipe_v1* gesture,
                        uint32_t serial, uint32_t time, struct wl_surface* surface,
                        uint32_t fingers) {
    (void)data; (void)gesture; (void)serial; (void)time; (void)surface; (void)fingers;
    begins++;
    printf("gesture began\n");
}

static void swipe_update(void* data, struct zwp_pointer_gesture_swipe_v1* gesture,
                         uint32_t time, wl_fixed_t dx, wl_fixed_t dy) {
    (void)data; (void)gesture; (void)time; (void)dx; (void)dy;
    updates++;
}

static void swipe_end(void* data, struct zwp_pointer_gesture_swipe_v1* gesture,
                      uint32_t serial, uint32_t time, int32_t cancelled) {
    (void)data; (void)gesture; (void)serial; (void)time; (void)cancelled;
    ends++;
}

static const struct zwp_pointer_gesture_swipe_v1_listener swipe_listener = {
    swipe_begin, swipe_update, swipe_end,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, zwp_pointer_gestures_v1_interface.name))
        gestures = wl_registry_bind(registry, name, &zwp_pointer_gestures_v1_interface, 3);
}

static void registry_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global, registry_remove,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!gestures || !wl_ptr) {
        fprintf(stderr, "missing gesture globals\n");
        return 1;
    }

    struct zwp_pointer_gesture_swipe_v1* swipe =
        zwp_pointer_gestures_v1_get_swipe_gesture(gestures, wl_ptr);
    zwp_pointer_gesture_swipe_v1_add_listener(swipe, &swipe_listener, NULL);

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "gesture-focus-loss", 400, 300, 0xFFFF0000);
    printf("gesture ready\n");

    for (int i = 0; i < 500 && !ends; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(10000);
    }

    if (begins != 1 || updates != 1 || ends != 1) {
        fprintf(stderr, "orphaned gesture: begin=%d update=%d end=%d\n",
                begins, updates, ends);
        return 1;
    }

    printf("gesture focus-loss ok\n");
    return 0;
}
