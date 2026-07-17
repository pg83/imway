// Feature: per-window keyboard layout. Two toplevels; the scenario toggles
// the xkb group (grp:caps_toggle) in one and checks via the state dump that
// refocusing restores each window's own group. This client just provides
// the windows.

#include "wl_util.h"

static struct wl_toplevel_ctx a, b;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    wl_make_toplevel(&a, "layA", 300, 200, 0xFFFF0000);
    wl_make_toplevel(&b, "layB", 300, 200, 0xFF0000FF);
    printf("two mapped\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
