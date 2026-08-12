/* PLAN limits #6: a large number of relative-pointer and gesture objects,
 * then a mass teardown. */
#include "wl_util.h"

#include <pointer-gestures-unstable-v1-client-protocol.h>
#include <relative-pointer-unstable-v1-client-protocol.h>

static struct zwp_relative_pointer_manager_v1* rel_mgr;
static struct zwp_pointer_gestures_v1* gestures;

static void ptr_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                       uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, zwp_relative_pointer_manager_v1_interface.name))
        rel_mgr = wl_registry_bind(r, name, &zwp_relative_pointer_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_pointer_gestures_v1_interface.name))
        gestures = wl_registry_bind(r, name, &zwp_pointer_gestures_v1_interface, 1);
}
static void ptr_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener ptr_listener = {ptr_global, ptr_remove};

#define N 256

static struct zwp_relative_pointer_v1* rels[N];
static struct zwp_pointer_gesture_swipe_v1* swipes[N / 2];
static struct zwp_pointer_gesture_pinch_v1* pinches[N / 2];

int main(void) {
    alarm(30);
    if (wl_boot() || !wl_seat_g) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &ptr_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!rel_mgr || !gestures) return 77;
    while (!wl_ptr && wl_display_dispatch(wl_dpy) != -1) {
    }

    for (int i = 0; i < N; i++)
        rels[i] = zwp_relative_pointer_manager_v1_get_relative_pointer(rel_mgr, wl_ptr);
    for (int i = 0; i < N / 2; i++) {
        swipes[i] = zwp_pointer_gestures_v1_get_swipe_gesture(gestures, wl_ptr);
        pinches[i] = zwp_pointer_gestures_v1_get_pinch_gesture(gestures, wl_ptr);
    }
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    for (int i = 0; i < N; i++) zwp_relative_pointer_v1_destroy(rels[i]);
    for (int i = 0; i < N / 2; i++) {
        zwp_pointer_gesture_swipe_v1_destroy(swipes[i]);
        zwp_pointer_gesture_pinch_v1_destroy(pinches[i]);
    }
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
