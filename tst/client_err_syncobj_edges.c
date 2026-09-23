// Explicit-sync points on a commit that has no buffer to go with them: a
// release point alone, and points set next to an attach of no buffer at
// all. Both are no_buffer. Exit 77 when explicit sync is unavailable.

#include "syncobj_error.inc"

int main(int argc, char** argv) {
    alarm(10);

    if (argc != 2) return 2;

    int rc = sync_test_boot();

    if (rc) return rc;

    struct wp_linux_drm_syncobj_timeline_v1* timeline = sync_test_timeline();

    if (!timeline) return 77;

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_linux_drm_syncobj_surface_v1* sync_surface =
        wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, surface);

    if (!strcmp(argv[1], "release-only")) {
        wp_linux_drm_syncobj_surface_v1_set_release_point(sync_surface, timeline, 0, 2);
        wl_surface_commit(surface);
        return wl_expect_error(wp_linux_drm_syncobj_surface_v1_interface.name, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_BUFFER);
    }

    if (!strcmp(argv[1], "null-attach")) {
        wl_surface_attach(surface, NULL, 0, 0);
        wp_linux_drm_syncobj_surface_v1_set_acquire_point(sync_surface, timeline, 0, 1);
        wp_linux_drm_syncobj_surface_v1_set_release_point(sync_surface, timeline, 0, 2);
        wl_surface_commit(surface);
        return wl_expect_error(wp_linux_drm_syncobj_surface_v1_interface.name, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_BUFFER);
    }

    return 2;
}
