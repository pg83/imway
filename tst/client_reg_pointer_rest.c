// A window that reports its pointer focus as it changes: "pointer entered"
// and "pointer left", each as the event arrives. The scenario moves the
// pointer onto the window and off it in single jumps, and never again after
// either: the enter and the leave must still come.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ptr) {
        fprintf(stderr, "no pointer\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "pointer-rest", 300, 200, 0xFFFF0000);
    wl_display_roundtrip(wl_dpy);
    printf("pointer-rest mapped\n");

    struct wl_surface* focus = NULL;

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (wlp_focus != focus) {
            printf("pointer %s\n", wlp_focus ? "entered" : "left");
            focus = wlp_focus;
        }
    }
    return 0;
}
