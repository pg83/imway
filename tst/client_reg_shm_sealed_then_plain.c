// A 200x150 toplevel whose content moves between the two kinds of wl_shm
// pool: a sealed one, which a udmabuf host samples in place as a dma-buf
// ("sealed committed", green), then on KEY_A an ordinary one that can only
// be copied ("plain committed", blue).

#include "wl_util.h"

#include <fcntl.h>

static struct wl_buffer* sealed_solid(int w, int h, uint32_t argb) {
    int stride = w * 4;
    int size = stride * h;
    int fd = memfd_create("sealed-shm", MFD_ALLOW_SEALING);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("sealed memfd");
        exit(1);
    }

    uint32_t* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (pixels == MAP_FAILED) {
        perror("sealed mmap");
        exit(1);
    }

    for (int i = 0; i < w * h; i++) {
        pixels[i] = argb;
    }

    munmap(pixels, size);

    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) < 0) {
        perror("sealed fcntl");
        exit(1);
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buffer;
}

static void commit(struct wl_surface* surface, struct wl_buffer* buffer) {
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, 200, 150);
    wl_surface_commit(surface);
    wl_display_flush(wl_dpy);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) {
        return 1;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "sealed-then-plain", 200, 150, 0xffff0000);
    commit(top.surface, sealed_solid(200, 150, 0xff00ff00));
    printf("sealed committed\n");

    wlk_watch_key = 30; // KEY_A
    int done = 0;

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (!done && wlk_watch_hits >= 2) {
            commit(top.surface, wl_solid(200, 150, 0xff0000ff));
            done = 1;
            printf("plain committed\n");
        }
    }

    return 0;
}
