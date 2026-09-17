// wl_keyboard.enter carries the keys that are already down. A client that
// takes the focus mid-chord has to be told what is held, or its own idea of
// the modifier state starts out wrong. This client maps a second toplevel
// while a key is down and reads back what the enter for it carried.

#include "wl_util.h"

#include <linux/input-event-codes.h>

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    if (!wl_kbd) {
        fprintf(stderr, "no seat keyboard\n");
        return 2;
    }

    struct wl_toplevel_ctx first;

    wl_make_toplevel(&first, "enter-keys", 300, 200, 0xFF804020u);

    while (!wlk_enters && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("client_reg_kb_enter_keys: focused\n");

    // the scenario holds a key down from here; it arrives as a key event on
    // the surface that has the focus now
    wlk_watch_key = KEY_A;

    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("client_reg_kb_enter_keys: key held\n");

    int before = wlk_enters;
    struct wl_toplevel_ctx second;

    wl_make_toplevel(&second, "enter-keys-second", 300, 200, 0xFF204080u);

    while (wlk_enters == before && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (wlk_focus != second.surface) {
        fprintf(stderr, "the focus did not move to the second toplevel\n");
        return 1;
    }

    if (wlk_enter_nkeys < 1) {
        fprintf(stderr, "the enter carried no pressed keys\n");
        return 1;
    }

    if (wlk_enter_keys[0] != KEY_A) {
        fprintf(stderr, "the enter carried key %u, not %u\n",
                wlk_enter_keys[0], (unsigned)KEY_A);
        return 1;
    }

    printf("client_reg_kb_enter_keys: enter carried %d key(s)\n", wlk_enter_nkeys);

    return 0;
}
