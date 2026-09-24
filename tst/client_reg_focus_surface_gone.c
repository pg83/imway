// The focused window's wl_surface is destroyed while its xdg_toplevel
// lives on (the role goes inert). A keyboard bound after that is sent no
// enter for the dead surface, and the next window mapped takes the focus
// with no leave sent for the old one.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_kbd) return 1;

    struct wl_toplevel_ctx gone, next;

    wl_make_toplevel(&gone, "focus-surface-gone", 200, 150, 0xffff0000);
    while (wlk_focus != gone.surface && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_surface_destroy(gone.surface);
    wl_display_roundtrip(wl_dpy);

    int enters = wlk_enters, leaves = wlk_leaves;
    struct wl_keyboard* late = wl_seat_get_keyboard(wl_seat_g);

    wl_keyboard_add_listener(late, &wlk_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
    if (wlk_enters != enters) {
        fprintf(stderr, "a late keyboard entered a destroyed surface\n");
        return 1;
    }

    wl_make_toplevel(&next, "focus-surface-next", 200, 150, 0xff00ff00);
    while (wlk_enters < enters + 2 && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);
    if (wlk_focus != next.surface || wlk_leaves != leaves) {
        fprintf(stderr, "focus %s the next window, %d leaves\n", wlk_focus == next.surface ? "on" : "not on",
                wlk_leaves - leaves);
        return 1;
    }

    printf("focus moved past the destroyed surface\n");

    return 0;
}
