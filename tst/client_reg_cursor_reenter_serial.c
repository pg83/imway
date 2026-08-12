#include "wl_util.h"
#include <linux/input-event-codes.h>

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(25);
    if (wl_boot() || !wl_ptr) return 1;
    struct wl_toplevel_ctx first;
    wl_make_toplevel(&first, "cursor-reenter-first", 200, 140, 0xffff0000);
    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    printf("cursor reenter ready\n");

    while (wlp_focus != first.surface && wl_display_dispatch(wl_dpy) != -1) {}
    uint32_t stale = wlp_enter_serial;
    printf("first enter\n");

    wl_surface_attach(first.surface, NULL, 0, 0);
    wl_surface_commit(first.surface);
    wl_display_flush(wl_dpy);
    while (wlp_focus && wl_display_dispatch(wl_dpy) != -1) {}
    printf("pointer left\n");
    first.committed = 0; // empty initial commit, then configure/ack/buffer
    wl_surface_commit(first.surface);
    wl_display_flush(wl_dpy);
    while (!first.committed && wl_display_dispatch(wl_dpy) != -1) {}
    printf("surface remapped\n");
    while ((wlp_focus != first.surface || wlp_enter_serial == stale) &&
           wl_display_dispatch(wl_dpy) != -1) {}

    wl_pointer_set_cursor(wl_ptr, stale, cursor, 0, 0);
    wl_display_roundtrip(wl_dpy);
    printf("stale cursor sent\n");
    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {}
    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor, 0, 0);
    wl_display_roundtrip(wl_dpy);
    printf("current cursor sent\n");
    wlk_watch_key = KEY_2; wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {}
    return 0;
}
