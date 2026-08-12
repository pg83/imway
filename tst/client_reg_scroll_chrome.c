// Regression: scroll routing at the client/chrome border. Axis events over
// client content must reach the client; a scroll over the compositor's menu
// bar must not. The scenario ends with the KEY_M sentinel, after which any
// extra axis event is the bug.

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_toplevel_ctx top;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    wl_make_toplevel(&top, "scroll", 300, 200, 0xFF0000FF);
    wlk_watch_key = KEY_M;
    printf("ready\n");

    // phase A: a scroll over our content
    while (wlp_axis_count < 1 && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("phaseA ok\n");

    // phase B: scrolls over the menu bar until the sentinel key
    int base = wlp_axis_count;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    int extra = wlp_axis_count - base;
    printf("phaseB extra_axis=%d\n", extra);
    if (extra) {
        fprintf(stderr, "chrome scroll leaked %d axis event(s) to the client\n", extra);
        return 1;
    }
    printf("scroll ok\n");
    return 0;
}
