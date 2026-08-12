// Feature: unmap by null-attach, then remap. A green helper window keeps
// keyboard reachable while the red main window is unmapped. KEY_U unmaps
// the main window; KEY_R starts the remap: a fresh initial commit must be
// answered with a configure, after which the buffer goes back up.

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_toplevel_ctx helper, top;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (wl_boot()) return 1;

    wl_make_toplevel(&helper, "helper", 60, 60, 0xFF00FF00);
    wl_make_toplevel(&top, "unmap", 300, 200, 0xFFFF0000);
    printf("both mapped\n");

    wlk_watch_key = KEY_U;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_surface_attach(top.surface, NULL, 0, 0);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("unmapped\n");

    wlk_watch_key = KEY_R;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    // remap: back to the pre-initial-commit state — an empty commit must
    // draw a configure, the ctx handler acks it and attaches the buffer
    top.committed = 0;
    wl_surface_commit(top.surface);
    while (!top.committed && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);
    printf("remapped\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
