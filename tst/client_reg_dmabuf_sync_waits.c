// A 400x100 dark toplevel carrying a row of 20 orange 16x16 dma-buf
// subsurfaces, each its own dumb buffer: the renderer waits on 20 implicit
// sync files in one frame, more semaphores than it keeps ready. Prints
// "cells committed"; exits 77 without dumb-buffer dma-bufs.

#include "wl_util.h"
#include "dumb_dmabuf.inc"

#define CELLS 20

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;
    int rc = dumb_boot();
    if (rc) return rc;

    static struct wl_buffer* buffers[CELLS];
    for (int i = 0; i < CELLS; i++) {
        buffers[i] = dumb_buffer(16, 16, 0xffff8000);
        if (!buffers[i]) return 77;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "sync-waits", 400, 100, 0xff202020);

    for (int i = 0; i < CELLS; i++) {
        struct wl_surface* cell = wl_compositor_create_surface(wl_comp);
        struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, cell, top.surface);
        wl_subsurface_set_position(sub, i * 20 + 2, 42);
        wl_surface_attach(cell, buffers[i], 0, 0);
        wl_surface_damage(cell, 0, 0, 16, 16);
        wl_surface_commit(cell);
    }
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("cells committed %d\n", CELLS);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
