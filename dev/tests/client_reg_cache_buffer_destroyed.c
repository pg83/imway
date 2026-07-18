/* PLAN buffer lifetime #4: a sync subsurface commit caches its buffer; the
 * wl_buffer dies before the parent commit applies the cache. */
#include "wl_util.h"

int main(void) {
    alarm(10);
    if (wl_boot() || !wl_subcomp) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "cache-buffer-destroyed", 128, 128, 0xffff0000);

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    wl_subsurface_set_sync(sub);
    wl_subsurface_set_position(sub, 16, 16);

    struct wl_buffer* cached = wl_solid(32, 32, 0xff00ff00);
    wl_surface_attach(child, cached, 0, 0);
    wl_surface_damage(child, 0, 0, 32, 32);
    wl_surface_commit(child); /* parked in the sync cache */
    wl_buffer_destroy(cached);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    wl_surface_commit(top.surface); /* applies the cache */
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
