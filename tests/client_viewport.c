// Тестовый клиент viewporter: буфер 200x200 (левая половина зелёная, правая
// маджента), source = правая половина, destination = 150x150. В кадре должна
// быть только маджента, растянутая до 150x150.

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <viewporter-client-protocol.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct xdg_wm_base* wm_base;
static struct wp_viewporter* viewporter;
static struct wl_surface* surface;
static struct wp_viewport* viewport;
static int drawn;

static void registry_global(void* data, struct wl_registry* reg, uint32_t name,
                            const char* iface, uint32_t version) {
    (void)data;
    (void)version;
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, 1);
}

static void registry_global_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}

static const struct wl_registry_listener registry_listener = {registry_global,
                                                              registry_global_remove};

static void wm_base_ping(void* d, struct xdg_wm_base* wb, uint32_t serial) {
    (void)d;
    xdg_wm_base_pong(wb, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {wm_base_ping};

static void draw(void) {
    const int W = 200, H = 200;
    int stride = W * 4, size = stride * H;
    int fd = memfd_create("client-vp-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            px[y * W + x] = x < W / 2 ? 0xFF00FF00 : 0xFFFF00FF; // зелёный | маджента
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer* buf =
        wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wp_viewport_set_source(viewport, wl_fixed_from_int(100), wl_fixed_from_int(0),
                           wl_fixed_from_int(100), wl_fixed_from_int(200));
    wp_viewport_set_destination(viewport, 150, 150);

    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage(surface, 0, 0, W, H);
    wl_surface_commit(surface);
    drawn = 1;
    printf("client_viewport: закоммитил 200x200 с кропом → 150x150\n");
}

static void xdg_surface_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!drawn) draw();
}
static const struct xdg_surface_listener xdg_surface_listener = {xdg_surface_configure};

static void toplevel_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                               struct wl_array* states) {
    (void)d;
    (void)t;
    (void)w;
    (void)h;
    (void)states;
}
static void toplevel_close(void* d, struct xdg_toplevel* t) {
    (void)d;
    (void)t;
    exit(0);
}
static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

int main(void) {
    struct wl_display* display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "client_viewport: нет соединения с композитором\n");
        return 1;
    }
    struct wl_registry* reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !wm_base || !viewporter) {
        fprintf(stderr, "client_viewport: нет нужных глобалов (viewporter=%p)\n",
                (void*)viewporter);
        return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

    surface = wl_compositor_create_surface(compositor);
    viewport = wp_viewporter_get_viewport(viewporter, surface);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xs, &xdg_surface_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &toplevel_listener, NULL);
    xdg_toplevel_set_title(tl, "client_viewport");
    wl_surface_commit(surface);

    while (wl_display_dispatch(display) != -1) {
    }
    return 0;
}
