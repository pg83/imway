// The wayland end of the uinput pointer scenario: a window that reports the
// pointer entering it, horizontal scroll reaching it (a running count, so
// the scenario can tell a wheel burst from a finger burst), and hold
// gestures on it. It runs until the scenario touches "listener-done".

#include "wl_util.h"
#include <pointer-gestures-unstable-v1-client-protocol.h>

static struct zwp_pointer_gestures_v1* gestures;
static struct wl_toplevel_ctx top;
static int hold_begin, hold_end;

static void hd_b(void* d, struct zwp_pointer_gesture_hold_v1* g, uint32_t s, uint32_t t,
                 struct wl_surface* su, uint32_t f) {
    (void)d; (void)g; (void)s; (void)t; (void)su;
    hold_begin++;
    printf("hold begin %u\n", f);
}
static void hd_e(void* d, struct zwp_pointer_gesture_hold_v1* g, uint32_t s, uint32_t t, int32_t c) {
    (void)d; (void)g; (void)s; (void)t;
    hold_end++;
    printf("hold end cancelled=%d\n", c);
}
static const struct zwp_pointer_gesture_hold_v1_listener hd_listener = {hd_b, hd_e};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_pointer_gestures_v1_interface.name))
        gestures = wl_registry_bind(r, name, &zwp_pointer_gestures_v1_interface, 3);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(120);
    if (wl_boot()) return 1;
    if (!wl_ptr) { fprintf(stderr, "no pointer\n"); return 1; }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!gestures) { fprintf(stderr, "no pointer-gestures\n"); return 1; }

    zwp_pointer_gesture_hold_v1_add_listener(
        zwp_pointer_gestures_v1_get_hold_gesture(gestures, wl_ptr), &hd_listener, NULL);

    wl_make_toplevel(&top, "axes-test", 400, 300, 0xFF30A060u);
    printf("listener ready\n");

    char done[512];

    snprintf(done, sizeof(done), "%s/listener-done", getenv("XDG_RUNTIME_DIR"));

    int entered = 0, seen = 0, horizontal = 0;

    while (access(done, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "connection lost\n");
            return 1;
        }

        if (!entered && wlp_focus == top.surface) {
            entered = 1;
            printf("entered\n");
        }

        // the last axis event of the batch says which axis moved; the
        // scenario only ever scrolls one axis at a time
        if (wlp_axis_count != seen) {
            seen = wlp_axis_count;

            if (wlp_axis_which == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
                horizontal++;
                printf("hscroll %d\n", horizontal);
            }
        }

        usleep(10000);
    }

    printf("listener done: holds %d/%d\n", hold_begin, hold_end);
    return 0;
}
