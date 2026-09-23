// Subsurfaces with no buffer, one stacked below its parent and one above,
// take no pointer: the pointer over the window enters the parent surface.

#include "wl_util.h"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_subcomp || !wl_ptr) return 2;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "subsurface-empty-pick", 240, 160, 0xFFFF0000);

    struct wl_surface* below = wl_compositor_create_surface(wl_comp);
    struct wl_surface* above = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* below_sub = wl_subcompositor_get_subsurface(wl_subcomp, below, top.surface);
    struct wl_subsurface* above_sub = wl_subcompositor_get_subsurface(wl_subcomp, above, top.surface);

    wl_subsurface_place_below(below_sub, top.surface);
    wl_subsurface_place_above(above_sub, top.surface);
    wl_subsurface_set_desync(below_sub);
    wl_subsurface_set_desync(above_sub);
    wl_surface_commit(below);
    wl_surface_commit(above);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    int motions = wlp_motion_count;
    printf("ready\n");

    // the scenario's motion over the window, picked with the subsurfaces in
    while ((!wlp_focus || wlp_motion_count == motions) && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (wlp_focus != top.surface) {
        fprintf(stderr, "the pointer entered %s\n", wlp_focus == below ? "the empty subsurface below" : wlp_focus == above ? "the empty subsurface above" : "something else");
        return 1;
    }
    printf("parent entered\n");
    return 0;
}
