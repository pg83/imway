// Feature: input through a deep, restacked subsurface tree. The parent
// carries two overlapping subsurfaces (green above blue by default) plus a
// yellow grandchild nested inside the green one. Clic→pick must follow the
// visible z-order, and place_below + parent commit must flip it.
//   P  = (60,60) parent-local: green/blue overlap
//   P2 = (30,30) inside the grandchild (green-local (10,10))

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_toplevel_ctx top;
static struct wl_surface *s_green, *s_blue, *s_grand;
static struct wl_subsurface *sub_green, *sub_blue, *sub_grand;

static void commit_tree(void) {
    wl_surface_commit(s_grand);
    wl_surface_commit(s_green);
    wl_surface_commit(s_blue);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (wl_boot()) return 1;
    if (!wl_subcomp) { fprintf(stderr, "no subcompositor\n"); return 1; }

    wl_make_toplevel(&top, "deep", 300, 200, 0xFFFF0000);

    // green at (20,20) 100x80 with a yellow 40x30 grandchild at (10,10)
    s_green = wl_compositor_create_surface(wl_comp);
    sub_green = wl_subcompositor_get_subsurface(wl_subcomp, s_green, top.surface);
    wl_subsurface_set_position(sub_green, 20, 20);
    wl_subsurface_set_desync(sub_green);
    wl_surface_attach(s_green, wl_solid(100, 80, 0xFF00FF00), 0, 0);
    wl_surface_damage(s_green, 0, 0, 100, 80);

    s_grand = wl_compositor_create_surface(wl_comp);
    sub_grand = wl_subcompositor_get_subsurface(wl_subcomp, s_grand, s_green);
    wl_subsurface_set_position(sub_grand, 10, 10);
    wl_subsurface_set_desync(sub_grand);
    wl_surface_attach(s_grand, wl_solid(40, 30, 0xFFFFFF00), 0, 0);
    wl_surface_damage(s_grand, 0, 0, 40, 30);

    // blue at (50,50) 100x80, stacked above green by creation order
    s_blue = wl_compositor_create_surface(wl_comp);
    sub_blue = wl_subcompositor_get_subsurface(wl_subcomp, s_blue, top.surface);
    wl_subsurface_set_position(sub_blue, 50, 50);
    wl_subsurface_set_desync(sub_blue);
    wl_surface_attach(s_blue, wl_solid(100, 80, 0xFF0000FF), 0, 0);
    wl_surface_damage(s_blue, 0, 0, 100, 80);

    commit_tree();
    printf("state1\n");

    // click at P: blue is on top there
    while (wlp_focus != s_blue && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("blue picked\n");

    // click at P2: the grandchild
    while (wlp_focus != s_grand && wl_display_dispatch(wl_dpy) != -1) {
    }
    double gx = wl_fixed_to_double(wlp_x), gy = wl_fixed_to_double(wlp_y);
    if (gx < 7 || gx > 13 || gy < 7 || gy > 13) {
        fprintf(stderr, "grandchild enter at %.1f,%.1f, want ~10,10\n", gx, gy);
        return 1;
    }
    printf("grandchild picked\n");

    // restack: blue goes below green; applied on the parent commit
    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_subsurface_place_below(sub_blue, s_green);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("state2\n");

    // the same P click must now land on green
    while (wlp_focus != s_green && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("green picked\n");
    return 0;
}
