/* A toplevel with a distinctive app_id for the status-line rule: the bar
 * prints the focused window's app_id first, and nothing without focus. */
#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "chrome-appid-probe", 300, 200, 0xff3060c0);
    printf("chrome probe ready\n");

    while (wl_display_dispatch(wl_dpy) >= 0) {
    }

    return 0;
}
