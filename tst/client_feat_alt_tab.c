// Helper client for the alt-tab test: maps two toplevels and stays alive so
// the compositor has something to switch between.

#include "wl_util.h"

static struct wl_toplevel_ctx a, b;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    wl_make_toplevel(&a, "client_feat_alt_tab_A", 300, 200, 0xFFFF0000);
    wl_make_toplevel(&b, "client_feat_alt_tab_B", 300, 200, 0xFF0000FF);
    printf("client_feat_alt_tab: two toplevels mapped\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
