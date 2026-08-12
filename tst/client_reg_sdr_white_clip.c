// An all-white SDR toplevel; the scenario asserts the display tone map does
// not dim SDR white when nothing on screen can exceed the output's peak.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "sdr-white-clip", 400, 300, 0xFFFFFFFFu);
    printf("client_reg_sdr_white_clip: mapped\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
