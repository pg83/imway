// Feature: wl_surface.set_buffer_transform 180. A 200x120 buffer that is red
// on the left and blue on the right must, after a 180 rotation, keep its size
// but swap the colors left<->right (and top<->bottom).

#include "wl_util.h"

static struct wl_surface* surface;
static struct xdg_surface* xs;
static int mapped;

static struct wl_buffer* two_tone(int w, int h) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("flip", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { perror("memfd"); exit(1); }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[y * w + x] = (x < w / 2) ? 0xFFFF0000 : 0xFF0000FF; // left red, right blue
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* b = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return b;
}

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* s) {
    (void)d; (void)t; (void)w; (void)h; (void)s;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void xs_configure(void* d, struct xdg_surface* x, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(x, serial);
    if (mapped) return;
    mapped = 1;
    wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_180);
    wl_surface_attach(surface, two_tone(200, 120), 0, 0);
    wl_surface_damage(surface, 0, 0, 200, 120);
    wl_surface_commit(surface);
    printf("client_feat_transform_flip: committed 200x120 transform 180\n");
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "client_feat_transform_flip");
    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
