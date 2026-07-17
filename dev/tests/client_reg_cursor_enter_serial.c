// Regression: wl_pointer.set_cursor accepts only the latest wl_pointer.enter
// serial. A button serial from the same focused client must be ignored.

#include "wl_util.h"
#include <linux/input-event-codes.h>

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;
    if (!wl_ptr) {
        fprintf(stderr, "no pointer\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "cursor-enter-serial", 400, 300, 0xFFFF0000);
    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    printf("cursor serial ready\n");

    while ((!wlp_enter_serial || !wlp_button_count) && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_pointer_set_cursor(wl_ptr, wlp_button_serial, cursor, 0, 0);
    wl_display_roundtrip(wl_dpy);
    printf("invalid cursor sent\n");

    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor, 0, 0);
    wl_display_roundtrip(wl_dpy);
    printf("valid cursor sent\n");

    wlk_watch_key = KEY_2;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("cursor serial ok\n");
    return 0;
}
