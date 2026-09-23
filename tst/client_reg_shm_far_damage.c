// A red 200x150 toplevel that, on KEY_A, commits a blue buffer whose only
// damage lies far outside it ("blue committed"): the damage clips to
// nothing, which leaves the new buffer's content to the compositor's
// judgement of a whole-buffer change.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "far-damage", 200, 150, 0xffff0000);
    printf("mapped\n");

    wlk_watch_key = 30; // KEY_A
    int done = 0;

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (!done && wlk_watch_hits >= 2) {
            wl_surface_attach(top.surface, wl_solid(200, 150, 0xff0000ff), 0, 0);
            wl_surface_damage_buffer(top.surface, 5000, 5000, 10, 10);
            wl_surface_commit(top.surface);
            wl_display_flush(wl_dpy);
            done = 1;
            printf("blue committed\n");
        }
    }
    return 0;
}
