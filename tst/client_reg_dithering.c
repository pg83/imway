#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "dithering", 320, 180, 0xff808080);
    printf("dithering ready\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
