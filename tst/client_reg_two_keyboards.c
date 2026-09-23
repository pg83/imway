// One of two clients with keyboards, named by argv[1] and colored by
// argv[2] (0xAARRGGBB): it reports every key and modifiers event it gets, so
// the scenario can tell the focused client's keyboard got them and the
// other's did not. Exits when the go-exit file appears.

#include "wl_util.h"

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
