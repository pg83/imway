#include "wl_util.h"

// wl_pointer.set_cursor with a null surface hides the cursor over the
// client's window; the scenario reads the scene's cursor shape.

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot() || !wl_ptr) return 2;

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "cursor-hide", 300, 200, 0xFFFF0000u);
    printf("cursor hide mapped\n");

    while (!wlp_enter_count && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, NULL, 0, 0);
    wl_display_roundtrip(wl_dpy);
    printf("cursor hidden sent\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
