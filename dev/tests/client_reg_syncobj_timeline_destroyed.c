/* PLAN: timeline_destroyed_after_set — set pending sync points, destroy the
 * protocol timeline objects, then commit. The compositor must hold its own
 * refs on the imported timelines until the commit (and any frame using the
 * buffer) is done; no protocol error, no crash. */
#include "syncobj_error.inc"

int main(void) {
    alarm(10);
    int rc = sync_test_boot();
    if (rc) return rc;
    struct wp_linux_drm_syncobj_timeline_v1* acquire = sync_test_timeline_keep();
    struct wp_linux_drm_syncobj_timeline_v1* release = sync_test_timeline();
    struct wl_buffer* buffer = sync_test_dmabuf();
    if (!acquire || !release || !buffer) return 77;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_linux_drm_syncobj_surface_v1* sync_surface =
        wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, surface);
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(sync_surface, acquire, 0, 1);
    wp_linux_drm_syncobj_surface_v1_set_release_point(sync_surface, release, 0, 1);
    wl_surface_attach(surface, buffer, 0, 0);

    wp_linux_drm_syncobj_timeline_v1_destroy(acquire);
    wp_linux_drm_syncobj_timeline_v1_destroy(release);
    wl_surface_commit(surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    /* materialize the acquire point: the compositor's held ref is what it
     * waits on, the protocol object is already gone */
    uint64_t point = 1;
    drmSyncobjTimelineSignal(sync_signal_fd, &sync_signal_handle, &point, 1);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    return 0;
}
