// A large wl_shm commit used by the offload integration scenario. The
// compositor's test delay keeps its shared worker busy long enough for the
// scenario to prove that the Wayland/control loop remains responsive.

#include "wl_util.h"

static void commit_resizable(struct wl_surface* surface) {
    int w = 1024;
    int h = 768;
    int stride = w * 4;
    int size = stride * h;
    int grown = size + 4096;
    int fd = memfd_create("resizable-shm", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("resizable memfd");
        exit(1);
    }

    uint32_t* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (pixels == MAP_FAILED) {
        perror("resizable mmap");
        exit(1);
    }

    for (int i = 0; i < w * h; i++) {
        pixels[i] = 0xff20c060;
    }

    munmap(pixels, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, w, h);
    wl_surface_commit(surface);

    if (ftruncate(fd, grown) < 0) {
        perror("grow memfd");
        exit(1);
    }

    wl_shm_pool_resize(pool, grown);
    struct wl_buffer* tail = wl_shm_pool_create_buffer(pool, size, 1, 1, 4, WL_SHM_FORMAT_ARGB8888);

    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "pool resize failed while the old mapping was busy\n");
        exit(1);
    }

    wl_buffer_destroy(tail);
    wl_shm_pool_destroy(pool);
    close(fd);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (wl_boot()) {
        return 1;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "shm-offload", 1024, 768, 0xff20c060);
    commit_resizable(top.surface);
    printf("shm offload committed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
