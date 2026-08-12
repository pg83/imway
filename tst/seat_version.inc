#include "wl_util.h"

#ifndef SEAT_VERSION
#error SEAT_VERSION is required
#endif

static struct wl_seat* test_seat;
static struct wl_keyboard* test_keyboard;
static int capabilities, seat_name, repeat_info;

static void tk_keymap(void* d, struct wl_keyboard* k, uint32_t format,
                      int32_t fd, uint32_t size) {
    (void)d; (void)k; (void)format; (void)size; close(fd);
}
static void tk_enter(void* d, struct wl_keyboard* k, uint32_t serial,
                     struct wl_surface* surface, struct wl_array* keys) {
    (void)d; (void)k; (void)serial; (void)surface; (void)keys;
}
static void tk_leave(void* d, struct wl_keyboard* k, uint32_t serial,
                     struct wl_surface* surface) {
    (void)d; (void)k; (void)serial; (void)surface;
}
static void tk_key(void* d, struct wl_keyboard* k, uint32_t serial,
                   uint32_t time, uint32_t key, uint32_t state) {
    (void)d; (void)k; (void)serial; (void)time; (void)key; (void)state;
}
static void tk_modifiers(void* d, struct wl_keyboard* k, uint32_t serial,
                         uint32_t depressed, uint32_t latched,
                         uint32_t locked, uint32_t group) {
    (void)d; (void)k; (void)serial; (void)depressed; (void)latched;
    (void)locked; (void)group;
}
static void tk_repeat(void* d, struct wl_keyboard* k, int32_t rate, int32_t delay) {
    (void)d; (void)k; (void)rate; (void)delay; repeat_info++;
}
static const struct wl_keyboard_listener test_keyboard_listener = {
    tk_keymap, tk_enter, tk_leave, tk_key, tk_modifiers, tk_repeat,
};

static void test_capabilities(void* d, struct wl_seat* seat, uint32_t caps) {
    (void)d; capabilities++;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !test_keyboard) {
        test_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(test_keyboard, &test_keyboard_listener, NULL);
    }
}
static void test_name(void* d, struct wl_seat* seat, const char* name) {
    (void)d; (void)seat; (void)name; seat_name++;
}
static const struct wl_seat_listener test_seat_listener = {
    test_capabilities, test_name,
};

static void seat_global(void* d, struct wl_registry* registry, uint32_t id,
                        const char* interface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(interface, wl_seat_interface.name) && !test_seat) {
        test_seat = wl_registry_bind(registry, id, &wl_seat_interface, SEAT_VERSION);
        wl_seat_add_listener(test_seat, &test_seat_listener, NULL);
    }
}
static void seat_remove(void* d, struct wl_registry* registry, uint32_t id) {
    (void)d; (void)registry; (void)id;
}
static const struct wl_registry_listener test_registry_listener = {
    seat_global, seat_remove,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &test_registry_listener, NULL);
    // registry bind, seat capabilities (which creates the keyboard), then
    // keyboard initial events are three distinct request/event turns
    if (wl_display_roundtrip(wl_dpy) < 0 ||
        wl_display_roundtrip(wl_dpy) < 0 ||
        wl_display_roundtrip(wl_dpy) < 0 || !test_seat || !test_keyboard) return 1;

    int ok = capabilities;
    if (SEAT_VERSION == 1) ok = ok && !seat_name && !repeat_info;
    else if (SEAT_VERSION == 2) ok = ok && seat_name && !repeat_info;
    else ok = ok && seat_name && repeat_info;
    printf("seat-v%d capabilities=%d name=%d repeat=%d\n",
           SEAT_VERSION, capabilities, seat_name, repeat_info);
    return ok ? 0 : 1;
}
