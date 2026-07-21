// Horizontal gray gradient; on the PQ output path the fractional part of the
// quantized code sweeps the full range across the width, so dither flips are
// frequent somewhere in every stripe. The scenario compares two frames: a
// static pattern repeats the same structure, temporal dithering must vary it.

#include "wl_util.h"

#define W 320
#define H 180

static struct wl_surface* surface;
static int committed;

static struct wl_buffer* gradient(void) {
    int stride = W * 4, size = stride * H;
    int fd = memfd_create("gradient-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t g = 96 + (uint32_t)(x * 64 / W);
            px[y * W + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
        }
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf =
        wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!committed) {
        wl_surface_attach(surface, gradient(), 0, 0);
        wl_surface_damage(surface, 0, 0, W, H);
        wl_surface_commit(surface);
        committed = 1;
        printf("dither-temporal ready\n");
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &wl_tl_listener, NULL);
    xdg_toplevel_set_title(tl, "dither-temporal");
    xdg_toplevel_set_app_id(tl, "dither-temporal");
    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
