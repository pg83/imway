// Feature: wl_surface.damage is in surface coordinates; with buffer_scale 2
// the compositor must scale the rect to buffer coordinates before copying.
// A compositor treating it as buffer coords copies a half-size rect and
// leaves the square partially stale. Stepped by the scenario via KEY_1.

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int committed;
static uint32_t* px;
static struct wl_buffer* buf;

static struct wl_buffer* mapped_buffer(int w, int h, uint32_t argb, uint32_t** out) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("dscale-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { perror("memfd"); exit(1); }
    uint32_t* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) p[i] = argb;
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* b = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    *out = p;
    return b;
}

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!committed) {
        buf = mapped_buffer(200, 200, 0xFF00FFFF, &px); // cyan, 100x100 surface
        wl_surface_set_buffer_scale(surface, 2);
        wl_surface_attach(surface, buf, 0, 0);
        wl_surface_damage(surface, 0, 0, 100, 100);
        wl_surface_commit(surface);
        committed = 1;
        printf("state1\n");
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
    xdg_toplevel_set_app_id(tl, "dscale");
    wl_surface_commit(surface);

    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    // magenta square: buffer (100,100)+40x40 == surface (50,50)+20x20;
    // damage is given in SURFACE coordinates
    for (int y = 100; y < 140; y++)
        for (int x = 100; x < 140; x++) px[y * 200 + x] = 0xFFFF00FF;
    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage(surface, 50, 50, 20, 20);
    wl_surface_commit(surface);
    wl_display_roundtrip(wl_dpy);
    printf("state2\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
