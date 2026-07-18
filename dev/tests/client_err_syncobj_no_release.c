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
    wl_surface_attach(surface, wl_solid(8, 8, 0xff00ff00), 0, 0);
    wl_surface_commit(surface);
    return wl_expect_error(wp_linux_drm_syncobj_surface_v1_interface.name,
                           WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_RELEASE_POINT);
}
