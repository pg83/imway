/* PLAN: surface_destroyed_with_pending_points — set sync points, destroy the
 * wl_surface before ever committing, then tear the rest down. Client and
 * compositor must both finish cleanly. */
#include "syncobj_error.inc"

int main(void) {
    alarm(10);
    int rc = sync_test_boot();
    if (rc) return rc;
    struct wp_linux_drm_syncobj_timeline_v1* timeline = sync_test_timeline();
    if (!timeline) return 77;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_linux_drm_syncobj_surface_v1* sync_surface =
        wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, surface);
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(sync_surface, timeline, 0, 1);
    wp_linux_drm_syncobj_surface_v1_set_release_point(sync_surface, timeline, 0, 2);

    wl_surface_destroy(surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    wp_linux_drm_syncobj_surface_v1_destroy(sync_surface);
    wp_linux_drm_syncobj_timeline_v1_destroy(timeline);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    return 0;
}
