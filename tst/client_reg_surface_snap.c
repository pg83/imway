// Server-decorated toplevel filled with 1px red/blue horizontal stripes.
// Any subpixel offset of the surface quad (fractional title-bar height,
// unrounded draw position) bilinearly mixes adjacent rows; the scenario
// asserts every interior pixel is still purely red or purely blue.

#include "wl_util.h"
#include <xdg-decoration-unstable-v1-client-protocol.h>

static struct zxdg_decoration_manager_v1* deco_mgr;
static struct wl_surface* surface;
static int committed;

static void reg2_global(void* d, struct wl_registry* r, uint32_t name,
                        const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

static void deco_configure(void* d, struct zxdg_toplevel_decoration_v1* z, uint32_t m) {
    (void)d;
    (void)z;
    (void)m;
}
static const struct zxdg_toplevel_decoration_v1_listener deco_listener = {deco_configure};

static struct wl_buffer* stripes(int w, int h) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("stripe-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[y * w + x] = (y & 1) ? 0xFF0000FFu : 0xFFFF0000u;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf =
        wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!committed) {
        wl_surface_attach(surface, stripes(400, 300), 0, 0);
        wl_surface_damage(surface, 0, 0, 400, 300);
        wl_surface_commit(surface);
        committed = 1;
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!deco_mgr) {
        fprintf(stderr, "no xdg-decoration manager\n");
        return 1;
    }

    surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &wl_tl_listener, NULL);
    xdg_toplevel_set_title(tl, "surface-snap");
    xdg_toplevel_set_app_id(tl, "surface-snap");

    struct zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, tl);
    zxdg_toplevel_decoration_v1_add_listener(deco, &deco_listener, NULL);
    zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
