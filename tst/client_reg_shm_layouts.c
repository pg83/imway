// wl_shm buffers laid out in ways the zero-copy imports refuse, all legal:
// a 640x100 dark toplevel carrying eight 60x60 subsurfaces 80 pixels apart,
// each from a sealed memfd pool, each with a coloured top half and a white
// bottom half so a misread stride or offset shows:
//   0 red     stride 242, not a multiple of four
//   1 green   offset 2, not a multiple of four
//   2 blue    offset 64: aligned, but not to a page
//   3 yellow  a 14400-byte pool, not a whole number of pages
//   4 cyan    offset 0 of a two-buffer pool
//   5 magenta offset 16384 of that same pool
//   6 orange  stride 242 again, in a pool of whole pages
//   7 purple  offset 2 again, in a pool of whole pages
// Prints "layouts committed".

#include "wl_util.h"

#define W 60
#define H 60

static int sealed_pool(size_t size, uint8_t** map) {
    int fd = memfd_create("shm-layouts", MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, (off_t)size) < 0 || fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0) {
        perror("memfd");
        exit(1);
    }
    *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return fd;
}

static void paint(uint8_t* base, int stride, uint32_t top) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint32_t px = y < H / 2 ? top : 0xffffffffu;
            memcpy(base + (size_t)y * stride + (size_t)x * 4, &px, 4);
        }
    }
}

static struct wl_buffer* buffer_in(int fd, size_t size, int offset, int stride) {
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, (int32_t)size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, offset, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    return buf;
}

static struct wl_buffer* layout(int offset, int stride, size_t size, uint32_t top) {
    uint8_t* map = NULL;
    int fd = sealed_pool(size, &map);
    paint(map + offset, stride, top);
    munmap(map, size);
    struct wl_buffer* buf = buffer_in(fd, size, offset, stride);
    close(fd);
    return buf;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;
    if (!wl_subcomp) {
        fprintf(stderr, "no wl_subcompositor\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "shm-layouts", 640, 100, 0xff202020);

    struct wl_buffer* bufs[8];
    bufs[0] = layout(0, 242, 242 * H, 0xffff0000u);
    bufs[1] = layout(2, W * 4, 2 + W * 4 * H, 0xff00ff00u);
    bufs[2] = layout(64, W * 4, 64 + W * 4 * H, 0xff0000ffu);
    bufs[3] = layout(0, W * 4, W * 4 * H, 0xffffff00u);
    bufs[6] = layout(0, 242, 16384, 0xffff8000u);
    bufs[7] = layout(2, W * 4, 16384, 0xff8000ffu);

    uint8_t* map = NULL;
    size_t shared = 16384 + W * 4 * H;
    int fd = sealed_pool(shared, &map);
    paint(map, W * 4, 0xff00ffffu);
    paint(map + 16384, W * 4, 0xffff00ffu);
    munmap(map, shared);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, (int32_t)shared);
    bufs[4] = wl_shm_pool_create_buffer(pool, 0, W, H, W * 4, WL_SHM_FORMAT_ARGB8888);
    bufs[5] = wl_shm_pool_create_buffer(pool, 16384, W, H, W * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    for (int i = 0; i < 8; i++) {
        struct wl_surface* s = wl_compositor_create_surface(wl_comp);
        struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, s, top.surface);
        wl_subsurface_set_position(sub, 10 + i * 80, 20);
        wl_surface_attach(s, bufs[i], 0, 0);
        wl_surface_damage(s, 0, 0, W, H);
        wl_surface_commit(s);
    }
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("layouts committed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
