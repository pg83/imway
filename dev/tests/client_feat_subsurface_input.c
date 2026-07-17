// Feature: pointer routing through a subsurface stack. A desync green
// subsurface sits at (150,60) inside a red toplevel; a click into it must
// enter the SUBSURFACE with coords local to it, and a later move onto bare
// parent must re-enter the parent with parent-local coords.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_surface* sub_surface;
static struct wl_subsurface* sub;

static int near(double v, double want) {
    return v > want - 3 && v < want + 3;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;
    if (!wl_subcomp || !wl_ptr) { fprintf(stderr, "missing globals\n"); return 1; }

    wl_make_toplevel(&top, "subinput", 300, 200, 0xFFFF0000);

    sub_surface = wl_compositor_create_surface(wl_comp);
    sub = wl_subcompositor_get_subsurface(wl_subcomp, sub_surface, top.surface);
    wl_subsurface_set_position(sub, 150, 60);
    wl_subsurface_set_desync(sub);
    wl_surface_attach(sub_surface, wl_solid(100, 80, 0xFF00FF00), 0, 0);
    wl_surface_damage(sub_surface, 0, 0, 100, 80);
    wl_surface_commit(sub_surface);
    wl_surface_commit(top.surface); // applies the subsurface position
    wl_display_roundtrip(wl_dpy);
    printf("ready\n");

    // the scenario points into the subsurface at local (20,10)
    while (wlp_focus != sub_surface && wl_display_dispatch(wl_dpy) != -1) {
    }
    double sx = wl_fixed_to_double(wlp_x), sy = wl_fixed_to_double(wlp_y);
    if (!near(sx, 20) || !near(sy, 10)) {
        fprintf(stderr, "subsurface enter at %.1f,%.1f, want ~20,10\n", sx, sy);
        return 1;
    }
    printf("sub ok\n");

    // then onto the bare parent at parent-local (20,20)
    while (wlp_focus != top.surface && wl_display_dispatch(wl_dpy) != -1) {
    }
    sx = wl_fixed_to_double(wlp_x);
    sy = wl_fixed_to_double(wlp_y);
    if (!near(sx, 20) || !near(sy, 20)) {
        fprintf(stderr, "parent enter at %.1f,%.1f, want ~20,20\n", sx, sy);
        return 1;
    }
    printf("parent ok\n");
    return 0;
}
