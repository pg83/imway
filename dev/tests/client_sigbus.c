// Test wayland client: green 400x300 rectangle via wl_shm + xdg-shell.
// Lives until SIGTERM; this is the seed of our protocol-test client.

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

#define W 400
#define H 300

static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct xdg_wm_base* wm_base;
static struct wl_surface* surface;
static int configured, drawn;

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

static int pool_fd = -1;
static struct wl_buffer* evil_buffer;

static void draw(void) {
    int stride = W * 4;
    int size = stride * H;
    pool_fd = memfd_create("client-sigbus", 0);
    if (pool_fd < 0 || ftruncate(pool_fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool_fd, 0);
    for (int i = 0; i < W * H; i++) px[i] = 0xFF00FF00;
    munmap(px, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(shm, pool_fd, size);
    evil_buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    wl_surface_attach(surface, evil_buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, W, H);
    wl_surface_commit(surface);
    drawn = 1;
    printf("client_sigbus: committed %dx%d\n", W, H);
}

// after the first show: truncate the pool out from under the compositor and commit again
static void attack(struct wl_display* display) {
    wl_display_roundtrip(display);
    if (ftruncate(pool_fd, 0) < 0) {
        perror("ftruncate");
        exit(1);
    }
    wl_surface_attach(surface, evil_buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, W, H);
    wl_surface_commit(surface);
    wl_display_flush(display);
    printf("client_sigbus: pool truncated, commit sent\n");
    // the compositor must survive SIGBUS and kill us with a protocol error
    while (wl_display_dispatch(display) != -1) {
    }
    printf("client_sigbus: connection closed by compositor\n");
    exit(0);
}

static void xdg_surface_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    configured = 1;
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
        fprintf(stderr, "client_sigbus: cannot connect to compositor\n");
        return 1;
    }
    struct wl_registry* reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !wm_base) {
        fprintf(stderr, "client_sigbus: missing required globals (compositor=%p shm=%p wm_base=%p)\n",
                (void*)compositor, (void*)shm, (void*)wm_base);
        return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

    surface = wl_compositor_create_surface(compositor);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xs, &xdg_surface_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &toplevel_listener, NULL);
    xdg_toplevel_set_title(tl, "client_shm");
    xdg_toplevel_set_app_id(tl, "imway.client-sigbus");
    wl_surface_commit(surface); // initial commit without a buffer → wait for configure

    while (!drawn && wl_display_dispatch(display) != -1) {
    }
    attack(display);
    return 0;
}
