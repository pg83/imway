#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(15);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "render-fault-victim", 240, 160, 0xffff0000);
    printf("render fault ready\n");

    while (wl_display_dispatch(wl_dpy) >= 0) {
    }

    return 0;
}
