// Two sealed wl_shm commits. Sealing makes the pool mapping stable enough
// for VK_EXT_external_memory_host; waiting for release also checks that a
// second use cannot overwrite source state while the first GPU read lives.

#include "wl_util.h"

#include <fcntl.h>

static int released;

static void buffer_release(void* data, struct wl_buffer* buffer) {
    (void)data;
    (void)buffer;
    released = 1;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

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

static struct wl_buffer* commit_sealed(struct wl_surface* surface) {
    struct wl_buffer* buffer = sealed_solid(1024, 768, 0xff20c060);

    wl_buffer_add_listener(buffer, &buffer_listener, NULL);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, 1024, 768);
    wl_surface_commit(surface);
    wl_display_flush(wl_dpy);

    return buffer;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (wl_boot()) {
        return 1;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "shm-external-host", 1024, 768, 0xff20c060);
    struct wl_buffer* first = commit_sealed(top.surface);

    while (!released && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (!released) {
        return 1;
    }

    printf("first sealed buffer released\n");
    wl_buffer_destroy(first);
    released = 0;
    commit_sealed(top.surface);
    printf("second sealed buffer committed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
