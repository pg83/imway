#include "syncobj_error.inc"

int main(void) {
    alarm(10);
    int rc = sync_test_boot();
    if (rc) return rc;
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, surface);
    wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, surface);
    return wl_expect_error(wp_linux_drm_syncobj_manager_v1_interface.name,
                           WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_SURFACE_EXISTS);
}
