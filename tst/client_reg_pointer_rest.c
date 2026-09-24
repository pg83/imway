// Windows that report the pointer focus as it changes: "pointer entered
// first|second" and "pointer left", each as it arrives. The scenario moves
// the pointer onto the first window and off it in single jumps, and never
// again after either: the enter and the leave must still come. Then, the
// pointer resting on the first, the scenario has the second window map
// over that spot: the pointer goes to it with no motion at all.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ptr) {
        fprintf(stderr, "no pointer\n");
        return 1;
    }

    struct wl_toplevel_ctx first, second;
    wl_make_toplevel(&first, "pointer-rest", 300, 200, 0xFFFF0000);
    wl_display_roundtrip(wl_dpy);
    printf("pointer-rest mapped\n");

    struct wl_surface* focus = NULL;
    int mapped = 0;

    while (wl_display_roundtrip(wl_dpy) != -1) {
        if (wlp_focus != focus) {
            if (wlp_focus) {
                printf("pointer entered %s\n", wlp_focus == first.surface ? "first" : "second");
            } else {
                printf("pointer left\n");
            }
            focus = wlp_focus;
        }

        if (!mapped && access("go-second", F_OK) == 0) {
            wl_make_toplevel(&second, "pointer-rest-second", 300, 200, 0xFF0000FF);
            wl_display_roundtrip(wl_dpy);
            printf("second mapped\n");
            mapped = 1;
        }

        usleep(20000);
    }
    return 0;
}
