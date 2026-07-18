/* PLAN depth #3: a wide tree — one parent with 1024 sibling subsurfaces, then
 * every sibling restacked. Each visible sibling needs its own texture
 * descriptor; VkTexturePool grows its pool chain on demand, so this many
 * concurrent textures must not exhaust anything or crash the compositor. */
#include "wl_util.h"

#define SIBLINGS 1024

static struct wl_surface* sib_surface[SIBLINGS];
static struct wl_subsurface* sib_sub[SIBLINGS];

int main(void) {
    alarm(60);
    if (wl_boot() || !wl_subcomp) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "wide-tree", 128, 128, 0xffff0000);

    struct wl_buffer* dot = wl_solid(2, 2, 0xff00ff00);
    for (int i = 0; i < SIBLINGS; i++) {
        sib_surface[i] = wl_compositor_create_surface(wl_comp);
        sib_sub[i] = wl_subcompositor_get_subsurface(wl_subcomp, sib_surface[i], top.surface);
        wl_subsurface_set_desync(sib_sub[i]);
        wl_subsurface_set_position(sib_sub[i], i % 64, i / 64);
        wl_surface_attach(sib_surface[i], dot, 0, 0);
        wl_surface_commit(sib_surface[i]);
    }
    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    /* restack every sibling: odd ones above the parent, even ones below
     * their neighbour */
    for (int i = 0; i < SIBLINGS; i++) {
        if (i & 1)
            wl_subsurface_place_above(sib_sub[i], top.surface);
        else
            wl_subsurface_place_below(sib_sub[i], sib_surface[i ? i - 1 : SIBLINGS - 1]);
    }
    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    for (int i = 0; i < SIBLINGS; i++) {
        wl_subsurface_destroy(sib_sub[i]);
        wl_surface_destroy(sib_surface[i]);
    }
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
