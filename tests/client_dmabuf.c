// Тестовый клиент dmabuf: memfd → udmabuf → zwp_linux_dmabuf_v1 (LINEAR) →
// оранжевый toplevel 320x240. Выход 77, если /dev/udmabuf недоступен (skip).

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/udmabuf.h>

#include <linux-dmabuf-v1-client-protocol.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

#define W 320
#define H 240
#define FOURCC_ARGB8888 0x34325241 /* 'AR24' */

static struct wl_compositor* compositor;
static struct xdg_wm_base* wm_base;
static struct zwp_linux_dmabuf_v1* dmabuf;
static struct wl_surface* surface;
static struct wl_buffer* buffer;
static int drawn, linear_ok;

static void dmabuf_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t fmt) {
    (void)d;
    (void)z;
    (void)fmt;
}
static void dmabuf_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t fmt, uint32_t hi,
                            uint32_t lo) {
    (void)d;
    (void)z;
    if (fmt == FOURCC_ARGB8888 && hi == 0 && lo == 0) linear_ok = 1;
}
static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {dmabuf_format,
                                                                    dmabuf_modifier};

static void registry_global(void* data, struct wl_registry* reg, uint32_t name,
                            const char* iface, uint32_t version) {
    (void)data;
    (void)version;
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(reg, name, &zwp_linux_dmabuf_v1_interface, 3);
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

static int make_dmabuf_fd(size_t size) {
    int dev = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
    if (dev < 0) {
        fprintf(stderr, "client_dmabuf: нет /dev/udmabuf: %m\n");
        exit(77);
    }
    int mem = memfd_create("dmabuf-src", MFD_ALLOW_SEALING);
    if (mem < 0 || ftruncate(mem, size) < 0 ||
        fcntl(mem, F_ADD_SEALS, F_SEAL_SHRINK) < 0) { // udmabuf требует seal
        perror("memfd");
        exit(1);
    }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mem, 0);
    for (size_t i = 0; i < size / 4; i++) px[i] = 0xFFFF8000; // оранжевый ARGB
    munmap(px, size);

    struct udmabuf_create create = {0};
    create.memfd = mem;
    create.flags = UDMABUF_FLAGS_CLOEXEC;
    create.offset = 0;
    create.size = size;
    int buf_fd = ioctl(dev, UDMABUF_CREATE, &create);
    if (buf_fd < 0) {
        fprintf(stderr, "client_dmabuf: UDMABUF_CREATE: %m\n");
        exit(77);
    }
    close(mem);
    close(dev);
    return buf_fd;
}

static void draw(void) {
    long page = sysconf(_SC_PAGESIZE);
    size_t size = ((size_t)W * H * 4 + page - 1) / page * page; // udmabuf: кратно странице
    int fd = make_dmabuf_fd(size);

    struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, W * 4, 0, 0); // LINEAR
    buffer = zwp_linux_buffer_params_v1_create_immed(params, W, H, FOURCC_ARGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    close(fd);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, W, H);
    wl_surface_commit(surface);
    drawn = 1;
    printf("client_dmabuf: закоммитил dmabuf %dx%d\n", W, H);
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
        fprintf(stderr, "client_dmabuf: нет соединения с композитором\n");
        return 1;
    }
    struct wl_registry* reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !wm_base || !dmabuf) {
        fprintf(stderr, "client_dmabuf: нет глобалов (dmabuf=%p)\n", (void*)dmabuf);
        return 1;
    }
    zwp_linux_dmabuf_v1_add_listener(dmabuf, &dmabuf_listener, NULL);
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
    wl_display_roundtrip(display); // получить modifier-события
    if (!linear_ok) {
        fprintf(stderr, "client_dmabuf: композитор не рекламирует ARGB8888+LINEAR\n");
        return 77;
    }

    surface = wl_compositor_create_surface(compositor);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xs, &xdg_surface_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &toplevel_listener, NULL);
    xdg_toplevel_set_title(tl, "client_dmabuf");
    wl_surface_commit(surface);

    while (wl_display_dispatch(display) != -1) {
    }
    return 0;
}
