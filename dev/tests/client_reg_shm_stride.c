// Regression (#1): a client can pass an shm buffer whose stride is smaller
// than width*4. libwayland only checks the pixel region fits the pool, not
// bytes-per-pixel, so width=64 stride=64 height=64 pool=4096 slips through;
// the compositor used to read 256 bytes per row from a 64-byte stride and
// walk off the mmap. It must now reject the buffer and stay alive.

#include "wl_util.h"

static struct wl_surface* surface;
static struct xdg_surface* xs;
static int done;

static void configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (done) return;
    done = 1;

    // 64x64 ARGB, but stride is 64 bytes (== width, a quarter of 64*4). The
    // pool is exactly width*height=4096 bytes, so libwayland lets it through.
    int w = 64, h = 64, stride = 64, size = 4096;
    int fd = memfd_create("bad-stride", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { perror("memfd"); exit(1); }
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                                      WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage(surface, 0, 0, w, h);
    wl_surface_commit(surface);
    printf("client_reg_shm_stride: committed 64x64 stride=64\n");
}
static const struct xdg_surface_listener xdg_listener = {configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xdg_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &wl_tl_listener, NULL);
    xdg_toplevel_set_title(tl, "client_reg_shm_stride");
    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
