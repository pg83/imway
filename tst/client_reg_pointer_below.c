// A subsurface stacked below its parent, in a corner the parent leaves out
// of its input region: the pointer there enters the subsurface.

#include "wl_util.h"

static struct wl_surface* below;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    if (wl_boot() || !wl_subcomp || !wl_ptr) return 2;

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "pointer-below", 200, 200, 0xFF4060C0u);

    below = wl_compositor_create_surface(wl_comp);

    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, below, top.surface);

    wl_subsurface_set_position(sub, 150, 150);

    struct wl_region* input = wl_compositor_create_region(wl_comp);

    wl_region_add(input, 0, 0, 150, 150);
    wl_surface_set_input_region(top.surface, input);
    wl_subsurface_place_below(sub, top.surface);
    wl_subsurface_set_desync(sub);
    wl_surface_attach(below, wl_solid(50, 50, 0xFFC06040u), 0, 0);
    wl_surface_damage(below, 0, 0, 50, 50);
    wl_surface_commit(below);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("pointer below: mapped\n");

    while (wlp_focus != below && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("pointer on the lower subsurface\n");

    return 0;
}
