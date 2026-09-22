#include "wl_util.h"

// A wl_keyboard created while its client already holds keyboard focus is
// entered at once, and the enter carries the keys held down right then —
// the client must not wait for the next focus change to learn either.

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (wl_boot()) return 2;

    if (!wl_kbd || !wl_seat_g) {
        fprintf(stderr, "no keyboard\n");
        return 2;
    }

    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "keyboard-late", 200, 200, 0xff806040u);

    while (!wlk_enters && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("focused\n");

    // the scenario holds KEY_A down
    wlk_watch_key = 30;

    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    int enters = wlk_enters;
    struct wl_keyboard* late = wl_seat_get_keyboard(wl_seat_g);

    wl_keyboard_add_listener(late, &wlk_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (wlk_enters != enters + 1 || wlk_focus != ctx.surface) {
        fprintf(stderr, "the late keyboard was not entered (%d enters, focus %p)\n",
                wlk_enters - enters, (void*)wlk_focus);
        return 1;
    }

    int held = 0;

    for (int i = 0; i < wlk_enter_nkeys; i++) {
        held |= wlk_enter_keys[i] == 30;
    }

    if (!held) {
        fprintf(stderr, "the late keyboard's enter does not carry the held key (%d keys)\n",
                wlk_enter_nkeys);
        return 1;
    }

    printf("late keyboard done\n");

    return 0;
}
