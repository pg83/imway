/* A magenta source window for the screenshot-viewer pipeline test: the color
 * is distinctive enough to be traced from the capture into the viewer. */
#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "shot-source", 400, 300, 0xffff00ff);
    printf("viewer source ready\n");

    while (wl_display_dispatch(wl_dpy) >= 0) {
    }

    return 0;
}
