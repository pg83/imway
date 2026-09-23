#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    // a safety net only: the scenarios end the client themselves, and a
    // loaded runner can take longer than a quarter minute to get there
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "render-fault-victim", 240, 160, 0xffff0000);
    printf("render fault ready\n");

    while (wl_display_dispatch(wl_dpy) >= 0) {
    }

    return 0;
}
