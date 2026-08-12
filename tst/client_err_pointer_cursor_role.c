#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_ptr) return 2;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "cursor-role", 240, 160, 0xffff0000);
    printf("mapped\n");
    while (!wlp_enter_serial && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, top.surface, 0, 0);
    return wl_expect_error(wl_pointer_interface.name, WL_POINTER_ERROR_ROLE);
}
