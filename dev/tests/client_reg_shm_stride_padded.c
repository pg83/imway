/* PLAN arithmetic #9: strides beyond the exact width*4 — row padding and a
 * very large but pool-backed stride must copy correctly; the release must
 * arrive. (stride < width*4 is covered by client_reg_shm_stride.) */
#include "wl_util.h"

static int releases;

static void buf_release(void* d, struct wl_buffer* b) {
    (void)d; (void)b;
    releases++;
}
static const struct wl_buffer_listener buf_listener = {buf_release};

static struct wl_buffer* strided(int w, int h, int stride, uint32_t argb) {
    int size = stride * h;
    int fd = memfd_create("stride-pool", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint8_t* base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int y = 0; y < h; y++) {
        uint32_t* row = (uint32_t*)(base + (size_t)y * stride);
        for (int x = 0; x < w; x++) row[x] = argb;
    }
    munmap(base, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf =
        wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "stride-padded", 64, 64, 0xffff0000);

    /* small padding, page padding, and a huge 1MB-per-row stride */
    static const int strides[3] = {64 * 4 + 4, 64 * 4 + 4096, 1 << 20};
    for (int i = 0; i < 3; i++) {
        struct wl_buffer* buf = strided(64, 64, strides[i], 0xff00ff00);
        wl_buffer_add_listener(buf, &buf_listener, NULL);
        wl_surface_attach(top.surface, buf, 0, 0);
        wl_surface_damage(top.surface, 0, 0, 64, 64);
        wl_surface_commit(top.surface);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        wl_buffer_destroy(buf);
    }
    for (int i = 0; i < 100 && releases < 3; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }
    if (releases < 3) {
        fprintf(stderr, "%d releases, want 3\n", releases);
        return 1;
    }
    return 0;
}
