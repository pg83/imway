// Feature: wl_subsurface.place_below restacking. Two overlapping subsurfaces
// (green A, blue B with B initially on top). Moving B below A must uncover A's
// pixels in the overlap and occlude B's — so visible green grows and visible
// blue shrinks.

#include "wl_util.h"

static struct wl_toplevel_ctx parent;
static struct wl_surface *a, *b;
static struct wl_subsurface *suba, *subb;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_subcomp) { fprintf(stderr, "no subcompositor\n"); return 1; }

    wl_make_toplevel(&parent, "client_feat_subsurface_restack", 300, 250, 0xFFFF0000);

    a = wl_compositor_create_surface(wl_comp);
    suba = wl_subcompositor_get_subsurface(wl_subcomp, a, parent.surface);
    wl_subsurface_set_desync(suba);
    wl_subsurface_set_position(suba, 40, 40);
    wl_surface_attach(a, wl_solid(100, 100, 0xFF00FF00), 0, 0); // green
    wl_surface_commit(a);

    b = wl_compositor_create_surface(wl_comp);
    subb = wl_subcompositor_get_subsurface(wl_subcomp, b, parent.surface);
    wl_subsurface_set_desync(subb);
    wl_subsurface_set_position(subb, 70, 70);
    wl_surface_attach(b, wl_solid(100, 100, 0xFF0000FF), 0, 0); // blue, added after A → on top
    wl_surface_commit(b);

    wl_surface_commit(parent.surface);
    for (int i = 0; i < 20; i++) { wl_display_roundtrip(wl_dpy); usleep(20000); }
    printf("client_feat_subsurface_restack: state1\n"); // B over A

    // hold state1 long enough for the scenario to screenshot it before we flip
    for (int i = 0; i < 150; i++) { wl_display_roundtrip(wl_dpy); usleep(20000); }

    // move B below A: A now wins the overlap
    wl_subsurface_place_below(subb, a);
    wl_surface_commit(parent.surface);
    for (int i = 0; i < 20; i++) { wl_display_roundtrip(wl_dpy); usleep(20000); }
    printf("client_feat_subsurface_restack: state2\n"); // A over B

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
