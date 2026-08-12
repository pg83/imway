#include "wl_util.h"

#include <input-method-unstable-v2-client-protocol.h>

// #F-13: the input method's keyboard grab intercepts physical keys. With a
// grab live, injected key events (control "key" via the FIFO) must arrive on
// the grab's key event, not on any application keyboard.

static struct zwp_input_method_manager_v2* im_mgr;
static struct wl_seat* seat2;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, zwp_input_method_manager_v2_interface.name))
        im_mgr = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat2)
        seat2 = wl_registry_bind(r, name, &wl_seat_interface, 5);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int grab_keymap_seen, grab_key_seen;
static uint32_t grab_key_code;

static void gk_keymap(void* d, struct zwp_input_method_keyboard_grab_v2* g,
                      uint32_t fmt, int32_t fd, uint32_t size) {
    (void)d; (void)g; (void)fmt; (void)size;
    close(fd);
    grab_keymap_seen = 1;
}
static void gk_key(void* d, struct zwp_input_method_keyboard_grab_v2* g,
                   uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    (void)d; (void)g; (void)serial; (void)time; (void)state;
    grab_key_code = key;
    grab_key_seen = 1;
}
static void gk_modifiers(void* d, struct zwp_input_method_keyboard_grab_v2* g,
                         uint32_t serial, uint32_t dep, uint32_t lat,
                         uint32_t lock, uint32_t group) {
    (void)d; (void)g; (void)serial; (void)dep; (void)lat; (void)lock; (void)group;
}
static void gk_repeat(void* d, struct zwp_input_method_keyboard_grab_v2* g,
                      int32_t rate, int32_t delay) {
    (void)d; (void)g; (void)rate; (void)delay;
}
static const struct zwp_input_method_keyboard_grab_v2_listener gk_listener = {
    .keymap = gk_keymap,
    .key = gk_key,
    .modifiers = gk_modifiers,
    .repeat_info = gk_repeat,
};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!im_mgr || !seat2) return 2;

    struct zwp_input_method_v2* im = zwp_input_method_manager_v2_get_input_method(im_mgr, seat2);
    struct zwp_input_method_keyboard_grab_v2* grab =
        zwp_input_method_v2_grab_keyboard(im);
    zwp_input_method_keyboard_grab_v2_add_listener(grab, &gk_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!grab_keymap_seen) {
        fprintf(stderr, "grab did not receive a keymap\n");
        return 1;
    }

    // tell the harness the grab is armed; it injects KEY_A (30) next
    printf("grab ready\n");
    fflush(stdout);

    while (!grab_key_seen && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (grab_key_code != 30) {
        fprintf(stderr, "grab saw key %u, want 30\n", grab_key_code);
        return 1;
    }

    printf("grab key done\n");
    fflush(stdout);
    return 0;
}
