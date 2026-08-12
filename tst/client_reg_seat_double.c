// Regression: the same client binding wl_seat twice. Both pointers and both
// keyboards must receive events; releasing one pointer must not break the
// other. wl_util's boot provides the first binding, a second registry the
// other.

#include "wl_util.h"

static struct wl_seat* seat2;
static struct wl_pointer* ptr2;
static struct wl_keyboard* kbd2;
static int p2_buttons, k2_keys;

static void s2_enter(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* su,
                     wl_fixed_t x, wl_fixed_t y) { (void)d; (void)p; (void)s; (void)su; (void)x; (void)y; }
static void s2_leave(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* su) {
    (void)d; (void)p; (void)s; (void)su;
}
static void s2_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)t; (void)x; (void)y;
}
static void s2_button(void* d, struct wl_pointer* p, uint32_t s, uint32_t t, uint32_t b,
                      uint32_t st) {
    (void)d; (void)p; (void)s; (void)t; (void)b; (void)st;
    p2_buttons++;
}
static void s2_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t a, wl_fixed_t v) {
    (void)d; (void)p; (void)t; (void)a; (void)v;
}
static void s2_frame(void* d, struct wl_pointer* p) { (void)d; (void)p; }
static void s2_asrc(void* d, struct wl_pointer* p, uint32_t s) { (void)d; (void)p; (void)s; }
static void s2_astop(void* d, struct wl_pointer* p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void s2_adisc(void* d, struct wl_pointer* p, uint32_t a, int32_t v) { (void)d; (void)p; (void)a; (void)v; }
static const struct wl_pointer_listener s2_ptr_listener = {
    .enter = s2_enter, .leave = s2_leave, .motion = s2_motion, .button = s2_button,
    .axis = s2_axis, .frame = s2_frame, .axis_source = s2_asrc,
    .axis_stop = s2_astop, .axis_discrete = s2_adisc,
};

static void k2_keymap(void* d, struct wl_keyboard* k, uint32_t f, int32_t fd, uint32_t s) {
    (void)d; (void)k; (void)f; (void)s;
    close(fd);
}
static void k2_enter(void* d, struct wl_keyboard* k, uint32_t s, struct wl_surface* su,
                     struct wl_array* keys) { (void)d; (void)k; (void)s; (void)su; (void)keys; }
static void k2_leave(void* d, struct wl_keyboard* k, uint32_t s, struct wl_surface* su) {
    (void)d; (void)k; (void)s; (void)su;
}
static void k2_key(void* d, struct wl_keyboard* k, uint32_t s, uint32_t t, uint32_t key,
                   uint32_t st) {
    (void)d; (void)k; (void)s; (void)t; (void)key; (void)st;
    k2_keys++;
}
static void k2_mods(void* d, struct wl_keyboard* k, uint32_t s, uint32_t a, uint32_t b,
                    uint32_t c, uint32_t g) { (void)d; (void)k; (void)s; (void)a; (void)b; (void)c; (void)g; }
static void k2_repeat(void* d, struct wl_keyboard* k, int32_t r, int32_t dl) { (void)d; (void)k; (void)r; (void)dl; }
static const struct wl_keyboard_listener s2_kbd_listener = {
    .keymap = k2_keymap, .enter = k2_enter, .leave = k2_leave,
    .key = k2_key, .modifiers = k2_mods, .repeat_info = k2_repeat,
};

static void s2_caps(void* d, struct wl_seat* s, uint32_t caps) {
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ptr2) {
        ptr2 = wl_seat_get_pointer(s);
        wl_pointer_add_listener(ptr2, &s2_ptr_listener, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !kbd2) {
        kbd2 = wl_seat_get_keyboard(s);
        wl_keyboard_add_listener(kbd2, &s2_kbd_listener, NULL);
    }
}
static void s2_name(void* d, struct wl_seat* s, const char* n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener s2_seat_listener = {s2_caps, s2_name};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wl_seat_interface.name) && !seat2) {
        seat2 = wl_registry_bind(r, name, &wl_seat_interface, 5);
        wl_seat_add_listener(seat2, &s2_seat_listener, NULL);
    }
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy); // second seat capabilities
    if (!ptr2 || !kbd2) { fprintf(stderr, "second seat did not come up\n"); return 1; }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "seat2", 300, 200, 0xFFFF0000);
    wlk_watch_key = 30; // KEY_A, typed by the scenario
    printf("ready\n");

    // the scenario clicks and types once
    while ((wlp_button_count < 2 || wlk_watch_hits < 1) && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (p2_buttons < 2) { fprintf(stderr, "second pointer missed buttons (%d)\n", p2_buttons); return 1; }
    if (k2_keys < 1) { fprintf(stderr, "second keyboard missed keys (%d)\n", k2_keys); return 1; }
    printf("both bindings saw input\n");

    // drop the first pointer; the second must keep receiving
    wl_pointer_release(wl_ptr);
    wl_ptr = NULL;
    wl_display_roundtrip(wl_dpy);
    int base = p2_buttons;
    printf("first pointer released\n");

    while (p2_buttons < base + 2 && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("ok\n");
    return 0;
}
