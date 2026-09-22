#include "wl_util.h"

// Two small windows for the follows-pointer focus policy scenario: it moves
// the pointer between them and reads which one has focus off the dump.

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) return 2;

    struct wl_toplevel_ctx a, b;

    wl_make_toplevel(&a, "ffp-a", 160, 120, 0xFFC03030u);
    wl_make_toplevel(&b, "ffp-b", 160, 120, 0xFF3030C0u);
    printf("windows mapped\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
