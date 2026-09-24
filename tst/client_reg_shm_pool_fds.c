// wl_shm pools on the fds a client may hand over besides a sealed memfd of
// the pool's size: a plain file (no seals to ask for) and a memfd sealed
// against shrinking but shorter than the pool claims (the tail past the
// file is never read). Each backs a window that must show its colour: the
// first green, the second blue, read from the part of each that exists.

#include "wl_util.h"

#include <fcntl.h>

static struct wl_buffer* pool_buffer(int fd, int file_size, int pool_size, int w, int h, uint32_t argb) {
    uint32_t* px = (uint32_t*)mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (px == MAP_FAILED) {
        perror("mmap");
        exit(2);
    }

    for (int i = 0; i < w * h; i++) {
        px[i] = argb;
    }

    munmap(px, file_size);

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, pool_size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buf;
}

static void show(struct wl_toplevel_ctx* ctx, struct wl_buffer* buf) {
    wl_surface_attach(ctx->surface, buf, 0, 0);
    wl_surface_damage_buffer(ctx->surface, 0, 0, ctx->w, ctx->h);
    wl_await_presented(ctx->surface);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (wl_boot()) return 2;

    const int w = 200, h = 150, size = w * h * 4;

    // a plain file in the working directory, unlinked from the start
    int plain = open(".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);

    if (plain < 0 || ftruncate(plain, size) < 0) {
        perror("O_TMPFILE");
        return 2;
    }

    // sealed against shrinking at the buffer's size, the pool twice as long
    int shortfd = memfd_create("short-pool", MFD_ALLOW_SEALING);

    if (shortfd < 0 || ftruncate(shortfd, size) < 0 || fcntl(shortfd, F_ADD_SEALS, F_SEAL_SHRINK) < 0) {
        perror("memfd");
        return 2;
    }

    struct wl_toplevel_ctx a, b;

    wl_make_toplevel(&a, "shm-plain-file", w, h, 0xff808080);
    show(&a, pool_buffer(plain, size, size, w, h, 0xff00ff00));
    wl_make_toplevel(&b, "shm-short-memfd", w, h, 0xff808080);
    show(&b, pool_buffer(shortfd, size, size * 2, w, h, 0xff0000ff));
    printf("pools shown\n");

    if (wl_await_file("go-quit")) {
        return 1;
    }

    return 0;
}
