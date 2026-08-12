#include "wl_util.h"

#ifndef GRAB_RESIZE
#define GRAB_RESIZE 0
#endif

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_ptr) return 1;
    struct wl_toplevel_ctx origin, victim;
    wl_make_toplevel(&origin, "wrong-grab-origin", 220, 150, 0xffff0000);
    wl_make_toplevel(&victim, "wrong-grab-victim", 220, 150, 0xff00ff00);
    printf("wrong grab ready\n");
    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {}
    struct wl_toplevel_ctx* target =
        wlp_focus == origin.surface ? &victim :
        wlp_focus == victim.surface ? &origin : NULL;
    if (!target) return 1;
#if GRAB_RESIZE
    xdg_toplevel_resize(target->tl, wl_seat_g, wlp_button_serial,
                        XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
    printf("wrong resize requested victim=%s\n",
           target == &victim ? "wrong-grab-victim" : "wrong-grab-origin");
#else
    xdg_toplevel_move(target->tl, wl_seat_g, wlp_button_serial);
    printf("wrong move requested victim=%s\n",
           target == &victim ? "wrong-grab-victim" : "wrong-grab-origin");
#endif
    wl_display_flush(wl_dpy);
    while (wl_display_dispatch(wl_dpy) != -1) {}
    return 0;
}
