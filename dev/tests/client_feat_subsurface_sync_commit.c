// Feature: sync subsurface commit semantics. A sync child's new buffer must
// stay CACHED until the parent commits: after committing yellow to the
// child alone the screen must still show green; the parent commit releases
// it. KEY_1 commits the child, KEY_2 the parent.

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_toplevel_ctx top;
static struct wl_surface* sub_surface;
static struct wl_subsurface* sub;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (wl_boot()) return 1;
    if (!wl_subcomp) { fprintf(stderr, "no subcompositor\n"); return 1; }

    wl_make_toplevel(&top, "synccache", 300, 200, 0xFFFF0000);

    sub_surface = wl_compositor_create_surface(wl_comp);
    sub = wl_subcompositor_get_subsurface(wl_subcomp, sub_surface, top.surface);
    wl_subsurface_set_position(sub, 50, 50);
    // sync is the default mode for a fresh subsurface
    wl_surface_attach(sub_surface, wl_solid(100, 80, 0xFF00FF00), 0, 0);
    wl_surface_damage(sub_surface, 0, 0, 100, 80);
    wl_surface_commit(sub_surface);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("state1\n");

    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    // yellow committed to the SYNC child only: must stay invisible
    wl_surface_attach(sub_surface, wl_solid(100, 80, 0xFFFFFF00), 0, 0);
    wl_surface_damage(sub_surface, 0, 0, 100, 80);
    wl_surface_commit(sub_surface);
    wl_display_roundtrip(wl_dpy);
    printf("state2\n");

    wlk_watch_key = KEY_2;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    // the parent commit applies the cached child state
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("state3\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
