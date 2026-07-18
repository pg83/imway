#include "wl_util.h"
#include <linux/input-event-codes.h>

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_ptr || !wl_kbd) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "input-health", 220, 140, 0xff00ff00);
    wlk_watch_key = KEY_1;
    printf("input-health ready\n");

    while ((wlp_button_count < 2 || wlk_watch_hits < 2) &&
           wl_display_dispatch(wl_dpy) != -1) {
    }

    if (wlp_button_count < 2 || wlk_watch_hits < 2) return 1;
    printf("input-health ok\n");
    return 0;
}
