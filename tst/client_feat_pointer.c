// Feature: wl_pointer delivery. A focused surface under the pointer must get
// enter (with a serial), motion, button (press+release with a serial), and
// axis (scroll) events. The scenario drives the input; the client asserts all
// four event classes arrived.

#include "wl_util.h"

static struct wl_toplevel_ctx top;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ptr) { fprintf(stderr, "client_feat_pointer: no pointer\n"); return 1; }

    wl_make_toplevel(&top, "client_feat_pointer", 400, 300, 0xFFFF0000);
    printf("client_feat_pointer: mapped\n");

    int got_enter = 0, got_btn = 0;
    for (int i = 0; i < 400; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        if (wlp_focus == top.surface && wlp_enter_serial) got_enter = 1;
        if (wlp_button_count > 0 && wlp_button_serial) got_btn = 1;

        if (got_enter && got_btn && wlp_motion_count > 0 && wlp_axis_count > 0) {
            printf("client_feat_pointer: enter(serial=%u) motion=%d button=%u(serial=%u) axis=%d\n",
                   wlp_enter_serial, wlp_motion_count, wlp_button, wlp_button_serial, wlp_axis_count);
            printf("client_feat_pointer: ok\n");
            return 0;
        }
        usleep(20000);
    }

    fprintf(stderr, "client_feat_pointer: missing events enter=%d motion=%d button=%d axis=%d\n",
            got_enter, wlp_motion_count, got_btn, wlp_axis_count);
    return 1;
}
