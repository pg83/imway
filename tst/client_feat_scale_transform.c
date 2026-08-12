// Feature: buffer_scale and buffer_transform combined. A 240x120 buffer
// (left half red, right half blue) with scale 2 and transform 90 must show
// as a 60x120 surface with the colors split along Y — a composition error in
// either factor breaks the size or the split axis. Runs until killed; the
// scenario checks pixels.

#include "wl_util.h"

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int committed;

static struct wl_buffer* halves_buffer(int w, int h, uint32_t left, uint32_t right) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("halves-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { perror("memfd"); exit(1); }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) px[y * w + x] = x < w / 2 ? left : right;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!committed) {
        wl_surface_set_buffer_scale(surface, 2);
        wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_90);
        wl_surface_attach(surface, halves_buffer(240, 120, 0xFFFF0000, 0xFF0000FF), 0, 0);
        wl_surface_damage(surface, 0, 0, 240, 120);
        wl_surface_commit(surface);
        committed = 1;
        printf("mapped\n");
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* s) {
    (void)d; (void)t; (void)w; (void)h; (void)s;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "scale-transform");
    xdg_toplevel_set_app_id(tl, "scale-transform");
    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
