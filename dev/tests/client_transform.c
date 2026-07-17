// wl_surface buffer-transform regression: a 120x200 buffer becomes a
// 200x120 logical surface after WL_OUTPUT_TRANSFORM_90.

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct xdg_wm_base* wm_base;
static struct wl_surface* surface;
static int drawn;

static void global(void* data, struct wl_registry* registry, uint32_t name,
                   const char* interface, uint32_t version) {
    (void)data;
    (void)version;
    if (!strcmp(interface, wl_compositor_interface.name))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, wl_shm_interface.name))
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
}

static void global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {global, global_remove};

static void ping(void* data, struct xdg_wm_base* base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener base_listener = {ping};

static void draw(void) {
    enum { W = 120, H = 200 };
    int stride = W * 4;
    int size = stride * H;
    int fd = memfd_create("transform-shm", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("transform shm");
        exit(1);
    }

    uint32_t* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            pixels[y * W + x] = y < H / 2 ? 0xffff0000u : 0xff0000ffu;

    munmap(pixels, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);
    wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_90);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, W, H);
    wl_surface_commit(surface);
    drawn = 1;
    puts("client_transform: committed");
}

static void surface_configure(void* data, struct xdg_surface* xdg, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(xdg, serial);
    if (!drawn) draw();
}

static const struct xdg_surface_listener surface_listener = {surface_configure};

static void toplevel_configure(void* data, struct xdg_toplevel* toplevel,
                               int32_t width, int32_t height, struct wl_array* states) {
    (void)data; (void)toplevel; (void)width; (void)height; (void)states;
}

static void toplevel_close(void* data, struct xdg_toplevel* toplevel) {
    (void)data; (void)toplevel;
    exit(0);
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

int main(void) {
    struct wl_display* display = wl_display_connect(NULL);

    if (!display) return 1;

    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !shm || !wm_base) return 1;

    xdg_wm_base_add_listener(wm_base, &base_listener, NULL);
    surface = wl_compositor_create_surface(compositor);
    struct xdg_surface* xdg = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg, &surface_listener, NULL);
    struct xdg_toplevel* toplevel = xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
    xdg_toplevel_set_title(toplevel, "client_transform");
    wl_surface_commit(surface);

    while (wl_display_dispatch(display) != -1) {}
    return 0;
}
