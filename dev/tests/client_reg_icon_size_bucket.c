// A toplevel whose app_id resolves to a store SVG. The scenario probes the
// resolved raster size at several desired sizes: the store must round each
// desired edge up to a power of two, and nearby sizes must share one raster.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx c;

    // app_id matches the staged imway-size-bucket.svg basename, so the store
    // name index resolves the window icon straight to the svg
    wl_make_toplevel(&c, "imway-size-bucket", 300, 200, 0xFF2040A0);
    wl_display_roundtrip(wl_dpy);
    printf("size-bucket mapped\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
