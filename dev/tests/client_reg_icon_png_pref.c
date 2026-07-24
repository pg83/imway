// A toplevel whose app_id resolves to a store icon that exists as both a
// fixed-size png and a scalable svg. The scenario probes which one the store
// picks at various desired sizes.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx c;

    wl_make_toplevel(&c, "imway-pref", 300, 200, 0xFF2040A0);
    wl_display_roundtrip(wl_dpy);
    printf("png-pref mapped\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
