// One of two clients with keyboards, named by argv[1] and colored by
// argv[2] (0xAARRGGBB): it reports every key and modifiers event it gets, so
// the scenario can tell the focused client's keyboard got them and the
// other's did not, and the enter and leave of a text input it never
// enables. Exits when the go-exit file appears.

#include "wl_util.h"
#include <text-input-unstable-v3-client-protocol.h>

static struct zwp_text_input_manager_v3* ti_mgr;

static void ti_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_text_input_manager_v3_interface.name))
        ti_mgr = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
}
static void ti_global_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener ti_registry_listener = {ti_global, ti_global_remove};

static void ti_enter(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
    printf("ti enter\n");
}
static void ti_leave(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
    printf("ti leave\n");
}
static void ti_preedit(void* d, struct zwp_text_input_v3* t, const char* s, int32_t a, int32_t b) {
    (void)d; (void)t; (void)s; (void)a; (void)b;
}
static void ti_commit(void* d, struct zwp_text_input_v3* t, const char* s) { (void)d; (void)t; (void)s; }
static void ti_delete(void* d, struct zwp_text_input_v3* t, uint32_t a, uint32_t b) { (void)d; (void)t; (void)a; (void)b; }
static void ti_done(void* d, struct zwp_text_input_v3* t, uint32_t s) { (void)d; (void)t; (void)s; }
static const struct zwp_text_input_v3_listener ti_listener = {
    .enter = ti_enter,
    .leave = ti_leave,
    .preedit_string = ti_preedit,
    .commit_string = ti_commit,
    .delete_surrounding_text = ti_delete,
    .done = ti_done,
};

static void key_ev(void* d, struct wl_keyboard* k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state) {
    wlk_key(d, k, serial, t, key, state);
    printf("key %u %s\n", key, state == WL_KEYBOARD_KEY_STATE_PRESSED ? "pressed" : "released");
}
static void mods_ev(void* d, struct wl_keyboard* k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) {
    wlk_mods(d, k, serial, dep, lat, lock, grp);
    printf("mods %u\n", dep);
}
static void enter_ev(void* d, struct wl_keyboard* k, uint32_t serial, struct wl_surface* s, struct wl_array* keys) {
    wlk_enter(d, k, serial, s, keys);
    printf("focused\n");
}
static const struct wl_keyboard_listener reporting_listener = {
    .keymap = wlk_keymap,
    .enter = enter_ev,
    .leave = wlk_leave,
    .key = key_ev,
    .modifiers = mods_ev,
    .repeat_info = wlk_repeat,
};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (argc < 3) return 2;

    // the keyboard is made here, with the reporting listener, before
    // wl_boot's seat capabilities would make one with the plain listener
    wl_dpy = wl_display_connect(NULL);
    if (!wl_dpy) return 2;
    wl_reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(wl_reg, &wl_reg_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!wl_comp || !wl_shm_g || !wl_wm || !wl_seat_g) return 2;
    xdg_wm_base_add_listener(wl_wm, &wl_wm_listener, NULL);
    wl_kbd = wl_seat_get_keyboard(wl_seat_g);
    wl_keyboard_add_listener(wl_kbd, &reporting_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    struct wl_registry* ti_reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(ti_reg, &ti_registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!ti_mgr) return 2;
    struct zwp_text_input_v3* ti = zwp_text_input_manager_v3_get_text_input(ti_mgr, wl_seat_g);
    zwp_text_input_v3_add_listener(ti, &ti_listener, NULL);

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, argv[1], 200, 140, (uint32_t)strtoul(argv[2], NULL, 16));
    printf("ready\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/go-exit", getenv("XDG_RUNTIME_DIR"));
    while (access(path, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }
    return 0;
}
