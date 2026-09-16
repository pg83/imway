// zwp_virtual_keyboard_v1: an IME or accessibility client synthesizes key
// events, and the compositor feeds them into the seat so they reach the
// focused surface exactly as a physical key would. This client is both
// ends of that trip: it maps a toplevel, takes focus, and then types at
// itself through a virtual keyboard.

#include "wl_util.h"

#include <linux/input-event-codes.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"

static struct zwp_virtual_keyboard_manager_v1* vkMgr;

static void vk_reg_global(void* d, struct wl_registry* r, uint32_t name,
                          const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name)) {
        vkMgr = wl_registry_bind(r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
}
static void vk_reg_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener vk_reg_listener = {vk_reg_global, vk_reg_remove};

// the compositor delivers its own seat keymap and closes ours, but a well
// formed one keeps the request honest
static int keymapFd(size_t* size) {
    static const char keymap[] =
        "xkb_keymap {\n"
        "  xkb_keycodes { minimum = 8; maximum = 255; };\n"
        "  xkb_types { };\n"
        "  xkb_compat { };\n"
        "  xkb_symbols { };\n"
        "};\n";

    int fd = memfd_create("vkeymap", 0);

    if (fd < 0) {
        return -1;
    }

    *size = sizeof(keymap);

    if (write(fd, keymap, sizeof(keymap)) != (ssize_t)sizeof(keymap)) {
        close(fd);

        return -1;
    }

    return fd;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg2, &vk_reg_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!vkMgr) {
        fprintf(stderr, "no zwp_virtual_keyboard_manager_v1\n");
        return 1;
    }

    if (!wl_seat_g || !wl_kbd) {
        fprintf(stderr, "no seat keyboard to receive the synthesized key\n");
        return 1;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "virtual-keyboard", 300, 200, 0xFF3080A0u);
    printf("client_reg_virtual_keyboard: mapped\n");

    // the key only reaches us once we hold the keyboard focus
    for (int i = 0; i < 400 && !wlk_enters; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "display error waiting for keyboard focus\n");
            return 1;
        }
    }

    if (!wlk_enters) {
        fprintf(stderr, "never took the keyboard focus\n");
        return 1;
    }

    struct zwp_virtual_keyboard_v1* vk =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vkMgr, wl_seat_g);

    if (!vk) {
        fprintf(stderr, "create_virtual_keyboard failed\n");
        return 1;
    }

    size_t size = 0;
    int fd = keymapFd(&size);

    if (fd < 0) {
        fprintf(stderr, "cannot build a keymap fd\n");
        return 1;
    }

    zwp_virtual_keyboard_v1_keymap(vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)size);
    close(fd);
    zwp_virtual_keyboard_v1_modifiers(vk, 0, 0, 0, 0);

    wlk_watch_key = KEY_A;
    zwp_virtual_keyboard_v1_key(vk, 1, KEY_A, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(vk, 2, KEY_A, WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush(wl_dpy);
    printf("client_reg_virtual_keyboard: typed\n");

    for (int i = 0; i < 400 && wlk_watch_hits < 2; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "display error waiting for the synthesized key\n");
            return 1;
        }
    }

    if (wlk_watch_hits < 2) {
        fprintf(stderr, "the synthesized key never came back (%d of 2)\n", wlk_watch_hits);
        return 1;
    }

    if (wlk_last_key != KEY_A) {
        fprintf(stderr, "the seat delivered key %u, not %u\n", wlk_last_key, (unsigned)KEY_A);
        return 1;
    }

    zwp_virtual_keyboard_v1_destroy(vk);
    zwp_virtual_keyboard_manager_v1_destroy(vkMgr);
    wl_registry_destroy(reg2);
    printf("client_reg_virtual_keyboard: key round trip\n");

    return 0;
}
