/* PLAN limits #7: fd-bearing requests in a loop — shm pools come and go, the
 * scenario watches the compositor's /proc fd count. */
#include "wl_util.h"

int main(void) {
    alarm(30);
    if (wl_boot()) return 1;

    for (int i = 0; i < 256; i++) {
        int size = 4 * 4 * 4;
        int fd = memfd_create("fd-churn", 0);
        if (fd < 0 || ftruncate(fd, size) < 0) return 1;
        struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
        struct wl_buffer* buf =
            wl_shm_pool_create_buffer(pool, 0, 4, 4, 16, WL_SHM_FORMAT_ARGB8888);
        wl_buffer_destroy(buf);
        wl_shm_pool_destroy(pool);
        close(fd);
        if ((i & 31) == 31 && wl_display_roundtrip(wl_dpy) < 0) return 1;
    }
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
