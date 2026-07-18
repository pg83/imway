#include "wl_util.h"
#include <pointer-gestures-unstable-v1-client-protocol.h>

#ifndef GESTURES_VERSION
#error GESTURES_VERSION is required
#endif

static struct zwp_pointer_gestures_v1* gestures;
static void gestures_global(void* d, struct wl_registry* registry, uint32_t id,
                            const char* interface, uint32_t version) {
    (void)d;
    if (!strcmp(interface, zwp_pointer_gestures_v1_interface.name) &&
        version >= GESTURES_VERSION && !gestures)
        gestures = wl_registry_bind(registry, id,
                                    &zwp_pointer_gestures_v1_interface,
                                    GESTURES_VERSION);
}
static void gestures_remove(void* d, struct wl_registry* registry, uint32_t id) {
    (void)d; (void)registry; (void)id;
}
static const struct wl_registry_listener gestures_registry_listener = {
    gestures_global, gestures_remove,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    if (wl_boot() || !wl_ptr) return 1;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &gestures_registry_listener, NULL);
    if (wl_display_roundtrip(wl_dpy) < 0 || !gestures ||
        zwp_pointer_gestures_v1_get_version(gestures) != GESTURES_VERSION) return 1;

    struct zwp_pointer_gesture_swipe_v1* swipe =
        zwp_pointer_gestures_v1_get_swipe_gesture(gestures, wl_ptr);
    struct zwp_pointer_gesture_pinch_v1* pinch =
        zwp_pointer_gestures_v1_get_pinch_gesture(gestures, wl_ptr);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    int ok = zwp_pointer_gesture_swipe_v1_get_version(swipe) == GESTURES_VERSION &&
             zwp_pointer_gesture_pinch_v1_get_version(pinch) == GESTURES_VERSION;
#if GESTURES_VERSION >= 3
    struct zwp_pointer_gesture_hold_v1* hold =
        zwp_pointer_gestures_v1_get_hold_gesture(gestures, wl_ptr);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    ok = ok && zwp_pointer_gesture_hold_v1_get_version(hold) == 3;
    printf("gestures-v3 hold-version=%u\n",
           zwp_pointer_gesture_hold_v1_get_version(hold));
#else
    printf("gestures-v1 swipe/pinch only\n");
#endif
    return ok ? 0 : 1;
}
