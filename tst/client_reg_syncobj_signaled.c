// Explicit sync with an acquire point that has already signaled: a 200x150
// dark toplevel carries a 64x64 orange dma-buf subsurface (a dumb buffer)
// whose commit names acquire point 1 of a timeline the client signals first,
// so the commit applies at once and the frame waits on the point's fence.
// Prints "signaled committed"; exits 77 without explicit sync or dumb
// buffers.

#include "syncobj_error.inc"
#include "dumb_dmabuf.inc"

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    int rc = sync_test_boot();
    if (rc) return rc;
    dumb_dmabuf_g = sync_dmabuf;
    if (!sync_dmabuf || !sync_linear_argb) return 77;

    struct wp_linux_drm_syncobj_timeline_v1* acquire = sync_test_timeline_keep();
    struct wp_linux_drm_syncobj_timeline_v1* release = sync_test_timeline();
    struct wl_buffer* buffer = dumb_buffer(64, 64, 0xffff8000);
    if (!acquire || !release || !buffer) return 77;

    uint64_t point = 1;
    if (drmSyncobjTimelineSignal(sync_signal_fd, &sync_signal_handle, &point, 1) != 0) {
        fprintf(stderr, "cannot signal the acquire point\n");
        return 77;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "syncobj-signaled", 200, 150, 0xff202020);

    struct wl_surface* cell = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, cell, top.surface);
    struct wp_linux_drm_syncobj_surface_v1* sync = wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, cell);

    wl_subsurface_set_position(sub, 68, 43);
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(sync, acquire, 0, 1);
    wp_linux_drm_syncobj_surface_v1_set_release_point(sync, release, 0, 1);
    wl_surface_attach(cell, buffer, 0, 0);
    wl_surface_damage(cell, 0, 0, 64, 64);
    wl_surface_commit(cell);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("signaled committed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
