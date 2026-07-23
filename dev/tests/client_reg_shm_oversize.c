/* PLAN: vk_check_audit — an shm buffer far beyond the device's texture limit
 * must degrade at commit (capped, logged, no content), not walk a
 * client-sized allocation and a Vulkan create into the top-level catch. The
 * client itself stays connected: oversize is cosmetic, not a kill. */
#include "wl_util.h"

#define BIG 17000

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "shm-oversize", 200, 150, 0xff20c040);

    /* a sparse memfd: the pool is huge on paper, no pages behind it */
    size_t stride = (size_t)BIG * 4, size = stride * BIG;
    int fd = memfd_create("oversize-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("oversize memfd");
        return 1;
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf =
        wl_shm_pool_create_buffer(pool, 0, BIG, BIG, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_attach(top.surface, buf, 0, 0);
    wl_surface_damage(top.surface, 0, 0, BIG, BIG);
    wl_surface_commit(top.surface);

    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "connection died on an oversized shm commit\n");
        return 1;
    }

    /* a normal commit afterwards must still work */
    wl_surface_attach(top.surface, wl_solid(200, 150, 0xff20c040), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 200, 150);
    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    printf("oversize survived\n");
    return 0;
}
