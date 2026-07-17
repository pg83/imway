// Regression (#4): a sync subsurface's pending frame callbacks are parked in
// sub->cache.frames, each pointing at the child SurfaceImpl. Destroying the
// child wl_surface before the wl_subsurface used to leave those cached
// callbacks with a dangling user_data; tearing down the subsurface then
// dereferenced the freed surface. Repro: sync subsurface, commit with a frame
// callback (fills the cache), destroy the wl_surface, then the wl_subsurface.

#include "wl_util.h"

static struct wl_toplevel_ctx top;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_subcomp) {
        fprintf(stderr, "client_reg_cache_frames_uaf: no subcompositor\n");
        return 1;
    }

    wl_make_toplevel(&top, "client_reg_cache_frames_uaf", 400, 300, 0xFFFF0000);
    printf("client_reg_cache_frames_uaf: mapped\n");

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    wl_subsurface_set_sync(sub);
    wl_subsurface_set_position(sub, 20, 20);

    // fill the child's pending frame list, then commit: sync caches it
    wl_surface_frame(child);
    wl_surface_attach(child, wl_solid(80, 80, 0xFF00FF00), 0, 0);
    wl_surface_commit(child);
    wl_display_roundtrip(wl_dpy);

    // destroy the wl_surface before its subsurface role — the cached callback
    // now outlives the SurfaceImpl it points at
    wl_surface_destroy(child);
    wl_subsurface_destroy(sub);
    wl_display_roundtrip(wl_dpy);

    printf("client_reg_cache_frames_uaf: torn down\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
