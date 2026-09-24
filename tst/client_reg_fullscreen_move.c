// A fullscreen window asks for an interactive move off a press on itself:
// the request is valid, but a fullscreen window does not move, so it is
// dropped and the window stays where fullscreen put it.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_ptr) return 1;

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "fullscreen-move", 320, 200, 0xffff0000);
    xdg_toplevel_set_fullscreen(top.tl, NULL);
    wl_surface_commit(top.surface);
    wl_await_presented(top.surface);
    printf("fullscreen move ready\n");

    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
    }

    xdg_toplevel_move(top.tl, wl_seat_g, wlp_button_serial);
    wl_display_flush(wl_dpy);
    printf("fullscreen move requested\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
