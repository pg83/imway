// pointer-gestures across focus and restarts. Gestures with no pointer
// focus reach nobody; a gesture that begins while one of its kind is still
// running cancels that one first (end with cancelled set, then the new
// begin); and a second client's gesture objects hear nothing of gestures
// on the focused client's surface.

#include "wl_util.h"
#include <pointer-gestures-unstable-v1-client-protocol.h>

struct counts {
    int begins, ends, cancelled, last_fingers;
};

static struct counts swipe, pinch, hold, other;

static void sw_b(void* d, struct zwp_pointer_gesture_swipe_v1* g, uint32_t s, uint32_t t,
                 struct wl_surface* su, uint32_t f) {
    (void)g; (void)s; (void)t; (void)su;
    struct counts* c = d;
    c->begins++;
    c->last_fingers = (int)f;
}
static void sw_u(void* d, struct zwp_pointer_gesture_swipe_v1* g, uint32_t t, wl_fixed_t dx, wl_fixed_t dy) {
    (void)d; (void)g; (void)t; (void)dx; (void)dy;
}
static void sw_e(void* d, struct zwp_pointer_gesture_swipe_v1* g, uint32_t s, uint32_t t, int32_t c) {
    (void)g; (void)s; (void)t;
    struct counts* n = d;
    n->ends++;
    n->cancelled += c != 0;
}
static const struct zwp_pointer_gesture_swipe_v1_listener sw_listener = {sw_b, sw_u, sw_e};

static void pn_b(void* d, struct zwp_pointer_gesture_pinch_v1* g, uint32_t s, uint32_t t,
                 struct wl_surface* su, uint32_t f) {
    (void)g; (void)s; (void)t; (void)su;
    struct counts* c = d;
    c->begins++;
    c->last_fingers = (int)f;
}
static void pn_u(void* d, struct zwp_pointer_gesture_pinch_v1* g, uint32_t t, wl_fixed_t dx,
                 wl_fixed_t dy, wl_fixed_t sc, wl_fixed_t rot) {
    (void)d; (void)g; (void)t; (void)dx; (void)dy; (void)sc; (void)rot;
}
static void pn_e(void* d, struct zwp_pointer_gesture_pinch_v1* g, uint32_t s, uint32_t t, int32_t c) {
    (void)g; (void)s; (void)t;
    struct counts* n = d;
    n->ends++;
    n->cancelled += c != 0;
}
static const struct zwp_pointer_gesture_pinch_v1_listener pn_listener = {pn_b, pn_u, pn_e};

static void hd_b(void* d, struct zwp_pointer_gesture_hold_v1* g, uint32_t s, uint32_t t,
                 struct wl_surface* su, uint32_t f) {
    (void)g; (void)s; (void)t; (void)su;
    struct counts* c = d;
    c->begins++;
    c->last_fingers = (int)f;
}
static void hd_e(void* d, struct zwp_pointer_gesture_hold_v1* g, uint32_t s, uint32_t t, int32_t c) {
    (void)g; (void)s; (void)t;
    struct counts* n = d;
    n->ends++;
    n->cancelled += c != 0;
}
static const struct zwp_pointer_gesture_hold_v1_listener hd_listener = {hd_b, hd_e};

static struct zwp_pointer_gestures_v1* found_gestures;
static struct wl_seat* found_seat;

static void reg_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_pointer_gestures_v1_interface.name))
        found_gestures = wl_registry_bind(r, name, &zwp_pointer_gestures_v1_interface, 3);
    else if (!strcmp(iface, wl_seat_interface.name) && !found_seat)
        found_seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
}
static void reg_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = {reg_global, reg_remove};

// every gesture kind on this display's pointer, all counted into `into`
static void listen(struct wl_display* dpy, struct wl_pointer* ptr, struct counts* sw,
                   struct counts* pn, struct counts* hd) {
    found_gestures = NULL;
    found_seat = NULL;

    struct wl_registry* reg = wl_display_get_registry(dpy);

    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);

    if (!found_gestures || !found_seat) {
        fprintf(stderr, "no pointer-gestures\n");
        exit(2);
    }

    if (!ptr)
        ptr = wl_seat_get_pointer(found_seat);

    zwp_pointer_gesture_swipe_v1_add_listener(
        zwp_pointer_gestures_v1_get_swipe_gesture(found_gestures, ptr), &sw_listener, sw);
    zwp_pointer_gesture_pinch_v1_add_listener(
        zwp_pointer_gestures_v1_get_pinch_gesture(found_gestures, ptr), &pn_listener, pn);
    zwp_pointer_gesture_hold_v1_add_listener(
        zwp_pointer_gestures_v1_get_hold_gesture(found_gestures, ptr), &hd_listener, hd);
    wl_display_roundtrip(dpy);
}

static int restarted(const struct counts* c) {
    return c->begins == 2 && c->ends == 2 && c->cancelled == 1 && c->last_fingers == 4;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    if (wl_boot() || !wl_ptr) return 2;

    // the bystander: its own connection, its own pointer and gestures
    struct wl_display* bystander = wl_display_connect(NULL);

    if (!bystander) return 2;

    listen(bystander, NULL, &other, &other, &other);
    listen(wl_dpy, wl_ptr, &swipe, &pinch, &hold);

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "gesture-restart", 400, 300, 0xff30a030u);
    printf("client_reg_gesture_restart: mapped\n");

    while (!wlp_enter_count && wl_display_dispatch(wl_dpy) != -1) {
    }

    // the unfocused gestures went out before the motion that brought the
    // pointer in, so anything they delivered is already here
    if (swipe.begins || pinch.begins || hold.begins) {
        fprintf(stderr, "a gesture without pointer focus was delivered\n");
        return 1;
    }

    printf("pointer entered\n");

    for (int i = 0; i < 500 && !(restarted(&swipe) && restarted(&pinch) && restarted(&hold)); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }

    wl_display_roundtrip(bystander);
    printf("swipe %d/%d/%d pinch %d/%d/%d hold %d/%d/%d bystander %d/%d\n",
           swipe.begins, swipe.ends, swipe.cancelled, pinch.begins, pinch.ends, pinch.cancelled,
           hold.begins, hold.ends, hold.cancelled, other.begins, other.ends);

    if (!restarted(&swipe) || !restarted(&pinch) || !restarted(&hold)) {
        fprintf(stderr, "a restarted gesture did not cancel the running one\n");
        return 1;
    }

    if (other.begins || other.ends) {
        fprintf(stderr, "the unfocused client heard the gestures\n");
        return 1;
    }

    printf("gesture restart done\n");

    return 0;
}
