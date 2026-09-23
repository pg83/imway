// wl_shm_pool requests out of bounds on one count each: a buffer at a
// negative offset, with no width, with no height, whose rows overflow a
// 32-bit size, larger than the pool, or running past the pool's end from
// its offset; and a pool resized to nothing. Each is its wl_shm error.

#include "wl_util.h"

#include <limits.h>

int main(int argc, char** argv) {
    alarm(10);

    if (argc != 2 || wl_boot()) return 2;

    const int size = 4096;
    int fd = memfd_create("shm-edges", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) return 2;

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    const char* mode = argv[1];
    const uint32_t fmt = WL_SHM_FORMAT_XRGB8888;

    close(fd);

    if (!strcmp(mode, "resize-zero")) {
        wl_shm_pool_resize(pool, 0);
        return wl_expect_error(wl_shm_pool_interface.name, WL_SHM_ERROR_INVALID_FD);
    }

    if (!strcmp(mode, "negative-offset")) {
        wl_shm_pool_create_buffer(pool, -4, 4, 4, 16, fmt);
    } else if (!strcmp(mode, "no-width")) {
        wl_shm_pool_create_buffer(pool, 0, 0, 4, 16, fmt);
    } else if (!strcmp(mode, "no-height")) {
        wl_shm_pool_create_buffer(pool, 0, 4, 0, 16, fmt);
    } else if (!strcmp(mode, "rows-overflow")) {
        wl_shm_pool_create_buffer(pool, 0, 1, 65536, 65536, fmt);
    } else if (!strcmp(mode, "past-pool")) {
        wl_shm_pool_create_buffer(pool, 0, 32, 64, 128, fmt);
    } else if (!strcmp(mode, "past-end")) {
        wl_shm_pool_create_buffer(pool, size - 2, 1, 1, 4, fmt);
    } else {
        return 2;
    }

    return wl_expect_error(wl_shm_pool_interface.name, WL_SHM_ERROR_INVALID_STRIDE);
}
