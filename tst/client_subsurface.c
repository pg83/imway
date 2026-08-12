// Test client: red 300x200 toplevel + two subsurfaces — a green sync 80x80
// at (40,40) and a blue desync 60x60 at (180,100); neither may end up below
// the parent: both are above. Checks sync semantics: the green one is
// committed BEFORE the parent's commit and must appear together with it.

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
static struct wl_subcompositor* subcompositor;
static struct wl_shm* shm;
static struct xdg_wm_base* wm_base;
static struct wl_surface* surface;
static struct wl_surface* sub_green;
static struct wl_surface* sub_blue;
static int configured, drawn;

static void registry_global(void* data, struct wl_registry* reg, uint32_t name,
                            const char* iface, uint32_t version) {
    (void)data;
    (void)version;
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_subcompositor_interface.name))
        subcompositor = wl_registry_bind(reg, name, &wl_subcompositor_interface, 1);
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

static struct wl_buffer* solid_buffer(int w, int h, uint32_t argb) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("client-sub-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) px[i] = argb;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                                      WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static void draw(void) {
    // green subsurface (sync by default): committed before the parent,
    // appears on screen only with the parent's commit
    wl_surface_attach(sub_green, solid_buffer(80, 80, 0xFF00FF00), 0, 0);
    wl_surface_commit(sub_green);

    wl_surface_attach(surface, solid_buffer(300, 200, 0xFFFF0000), 0, 0);
    wl_surface_damage(surface, 0, 0, 300, 200);
    wl_surface_commit(surface); // also applies the green one's cached state

    // blue — desync: committed after the parent, visible immediately
    wl_surface_attach(sub_blue, solid_buffer(60, 60, 0xFF0000FF), 0, 0);
    wl_surface_commit(sub_blue);

    drawn = 1;
    printf("client_subsurface: committed toplevel + 2 subsurfaces\n");
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
        fprintf(stderr, "client_subsurface: cannot connect to compositor\n");
        return 1;
    }
    struct wl_registry* reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !subcompositor || !shm || !wm_base) {
        fprintf(stderr, "client_subsurface: missing required globals\n");
        return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

    surface = wl_compositor_create_surface(compositor);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xs, &xdg_surface_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &toplevel_listener, NULL);
    xdg_toplevel_set_title(tl, "client_subsurface");

    sub_green = wl_compositor_create_surface(compositor);
    struct wl_subsurface* ss_green =
        wl_subcompositor_get_subsurface(subcompositor, sub_green, surface);
    wl_subsurface_set_position(ss_green, 40, 40); // sync (default)

    sub_blue = wl_compositor_create_surface(compositor);
    struct wl_subsurface* ss_blue =
        wl_subcompositor_get_subsurface(subcompositor, sub_blue, surface);
    wl_subsurface_set_position(ss_blue, 180, 100);
    wl_subsurface_set_desync(ss_blue);

    wl_surface_commit(surface); // initial commit without a buffer → wait for configure

    while (wl_display_dispatch(display) != -1) {
    }
    return 0;
}
