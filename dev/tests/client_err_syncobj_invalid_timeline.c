#include "syncobj_error.inc"

int main(void) {
    alarm(10);
    int rc = sync_test_boot();
    if (rc) return rc;
    int fd = memfd_create("not-a-syncobj", 0);
    wp_linux_drm_syncobj_manager_v1_import_timeline(sync_manager, fd);
    close(fd);
    return wl_expect_error(wp_linux_drm_syncobj_manager_v1_interface.name,
                           WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_INVALID_TIMELINE);
}
