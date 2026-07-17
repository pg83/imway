// Feature: pointer-gestures. A focused client with swipe/pinch/hold gesture
// objects must receive begin/update/end events when the compositor replays a
// touchpad gesture.

#include "wl_util.h"
#include <pointer-gestures-unstable-v1-client-protocol.h>

static struct zwp_pointer_gestures_v1* gestures;
static struct wl_toplevel_ctx top;
static int sw_begin, sw_update, sw_end, pn_begin, pn_update, pn_end, hd_begin, hd_end;

static void sw_b(void* d, struct zwp_pointer_gesture_swipe_v1* g, uint32_t s, uint32_t t,
                 struct wl_surface* su, uint32_t f) {
    (void)d; (void)g; (void)s; (void)t; (void)su; (void)f; sw_begin++;
}
static void sw_u(void* d, struct zwp_pointer_gesture_swipe_v1* g, uint32_t t, wl_fixed_t dx, wl_fixed_t dy) {
    (void)d; (void)g; (void)t; (void)dx; (void)dy; sw_update++;
}
static void sw_e(void* d, struct zwp_pointer_gesture_swipe_v1* g, uint32_t s, uint32_t t, int32_t c) {
    (void)d; (void)g; (void)s; (void)t; (void)c; sw_end++;
}
static const struct zwp_pointer_gesture_swipe_v1_listener sw_listener = {sw_b, sw_u, sw_e};

static void pn_b(void* d, struct zwp_pointer_gesture_pinch_v1* g, uint32_t s, uint32_t t,
                 struct wl_surface* su, uint32_t f) {
    (void)d; (void)g; (void)s; (void)t; (void)su; (void)f; pn_begin++;
}
static void pn_u(void* d, struct zwp_pointer_gesture_pinch_v1* g, uint32_t t, wl_fixed_t dx,
                 wl_fixed_t dy, wl_fixed_t sc, wl_fixed_t rot) {
    (void)d; (void)g; (void)t; (void)dx; (void)dy; (void)sc; (void)rot; pn_update++;
}
static void pn_e(void* d, struct zwp_pointer_gesture_pinch_v1* g, uint32_t s, uint32_t t, int32_t c) {
    (void)d; (void)g; (void)s; (void)t; (void)c; pn_end++;
}
static const struct zwp_pointer_gesture_pinch_v1_listener pn_listener = {pn_b, pn_u, pn_e};

static void hd_b(void* d, struct zwp_pointer_gesture_hold_v1* g, uint32_t s, uint32_t t,
                 struct wl_surface* su, uint32_t f) {
    (void)d; (void)g; (void)s; (void)t; (void)su; (void)f; hd_begin++;
}
static void hd_e(void* d, struct zwp_pointer_gesture_hold_v1* g, uint32_t s, uint32_t t, int32_t c) {
    (void)d; (void)g; (void)s; (void)t; (void)c; hd_end++;
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
    if (wl_boot()) return 1;
    if (!wl_ptr) { fprintf(stderr, "no pointer\n"); return 1; }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!gestures) { fprintf(stderr, "no pointer-gestures\n"); return 1; }

    zwp_pointer_gesture_swipe_v1_add_listener(
        zwp_pointer_gestures_v1_get_swipe_gesture(gestures, wl_ptr), &sw_listener, NULL);
    zwp_pointer_gesture_pinch_v1_add_listener(
        zwp_pointer_gestures_v1_get_pinch_gesture(gestures, wl_ptr), &pn_listener, NULL);
    zwp_pointer_gesture_hold_v1_add_listener(
        zwp_pointer_gestures_v1_get_hold_gesture(gestures, wl_ptr), &hd_listener, NULL);

    wl_make_toplevel(&top, "client_feat_gestures", 400, 300, 0xFFFF0000);
    printf("client_feat_gestures: mapped\n");

    for (int i = 0; i < 500; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        if (sw_begin && sw_update && sw_end && pn_begin && pn_update && pn_end && hd_begin && hd_end) {
            printf("client_feat_gestures: swipe(%d/%d/%d) pinch(%d/%d/%d) hold(%d/%d)\n",
                   sw_begin, sw_update, sw_end, pn_begin, pn_update, pn_end, hd_begin, hd_end);
            printf("client_feat_gestures: ok\n");
            return 0;
        }
        usleep(20000);
    }
    fprintf(stderr, "client_feat_gestures: incomplete swipe(%d/%d/%d) pinch(%d/%d/%d) hold(%d/%d)\n",
            sw_begin, sw_update, sw_end, pn_begin, pn_update, pn_end, hd_begin, hd_end);
    return 1;
}
