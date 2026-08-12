// Regression (#18): unmapping a toplevel by committing a null buffer used to
// leave keyboard focus pointing at the now-hidden surface — no leave was sent
// and keys kept flowing to it. Repro: map a toplevel (it takes keyboard
// focus, so we get wl_keyboard.enter), then unmap it and expect a
// wl_keyboard.leave.

#include "wl_util.h"

static struct wl_toplevel_ctx top;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_kbd) {
        fprintf(stderr, "client_reg_unmap_focus: no keyboard\n");
        return 1;
    }

    wl_make_toplevel(&top, "client_reg_unmap_focus", 400, 300, 0xFFFF0000);

    for (int i = 0; i < 50 && wlk_enters == 0; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (wlk_enters == 0) {
        fprintf(stderr, "client_reg_unmap_focus: never got keyboard focus\n");
        return 1;
    }
    printf("client_reg_unmap_focus: focused\n");

    // unmap: attach a null buffer and commit
    wl_surface_attach(top.surface, NULL, 0, 0);
    wl_surface_commit(top.surface);

    for (int i = 0; i < 50 && wlk_leaves == 0; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }

    if (wlk_leaves == 0) {
        fprintf(stderr, "client_reg_unmap_focus: no keyboard leave after unmap\n");
        return 1;
    }
    printf("client_reg_unmap_focus: got leave\n");
    return 0;
}
