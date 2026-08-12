// XRGB8888 ignores the alpha byte by definition, and clients commonly leave
// it zero (cairo RGB24, plain memset). The surface must still render fully
// opaque.

#include "wl_util.h"

static struct wl_surface* surface;
static int committed;

static struct wl_buffer* xrgb_solid(int w, int h, uint32_t xrgb) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("xrgb-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) px[i] = xrgb;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf =
        wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!committed) {
        // X byte deliberately zero: green with garbage "alpha"
        wl_surface_attach(surface, xrgb_solid(400, 300, 0x0000FF00u), 0, 0);
        wl_surface_damage(surface, 0, 0, 400, 300);
        wl_surface_commit(surface);
        committed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &wl_tl_listener, NULL);
    xdg_toplevel_set_title(tl, "xrgb-alpha");
    xdg_toplevel_set_app_id(tl, "xrgb-alpha");
    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
