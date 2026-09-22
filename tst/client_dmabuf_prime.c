// A 128x96 orange ARGB8888 dmabuf toplevel whose buffer is a DRM dumb buffer
// exported through PRIME from a card node, so it needs neither /dev/udmabuf
// nor a GPU of its own. Prints "committed dmabuf" once the buffer is on its
// surface and then stays connected until the compositor lets go of it; exits
// 77 when no card node hands out a dumb buffer or LINEAR is not offered.

#include "wl_util.h"

#include <xf86drm.h>

#include <linux-dmabuf-v1-client-protocol.h>

#include <sys/mman.h>

#define W 128
#define H 96
#define FOURCC_ARGB8888 0x34325241 /* 'AR24' */

static struct zwp_linux_dmabuf_v1* dmabuf;
static int linear_ok;

static void dmabuf_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t fmt) {
    (void)d;
    (void)z;
    (void)fmt;
}
static void dmabuf_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t fmt,
                            uint32_t hi, uint32_t lo) {
    (void)d;
    (void)z;
    if (fmt == FOURCC_ARGB8888 && hi == 0 && lo == 0) linear_ok = 1;
}
static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {dmabuf_format,
                                                                    dmabuf_modifier};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, 3);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int make_dmabuf_fd(uint32_t* pitch) {
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        struct drm_mode_create_dumb create = {0};
        create.width = W;
        create.height = H;
        create.bpp = 32;
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
            close(fd);
            continue;
        }

        struct drm_mode_map_dumb map = {0};
        map.handle = create.handle;
        int prime = -1;
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0 ||
            drmPrimeHandleToFD(fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &prime) != 0 || prime < 0) {
            close(fd);
            continue;
        }

        uint32_t* px = mmap(NULL, (size_t)create.pitch * H, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, map.offset);
        if (px != MAP_FAILED) {
            for (size_t j = 0; j < (size_t)create.pitch * H / 4; j++) px[j] = 0xFFFF8000u;
            munmap(px, (size_t)create.pitch * H);
        }
        close(fd); /* the prime fd keeps the buffer alive */
        *pitch = create.pitch;
        return prime;
    }
    fprintf(stderr, "no usable /dev/dri/card node\n");
    exit(77);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dmabuf) {
        fprintf(stderr, "no linux-dmabuf global\n");
        return 1;
    }
    zwp_linux_dmabuf_v1_add_listener(dmabuf, &dmabuf_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!linear_ok) {
        fprintf(stderr, "no ARGB8888+LINEAR\n");
        return 77;
    }

    uint32_t pitch = 0;
    int fd = make_dmabuf_fd(&pitch);
    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "dmabuf-prime", W, H, 0xff000000);

    struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, pitch, 0, 0); // LINEAR
    struct wl_buffer* buffer = zwp_linux_buffer_params_v1_create_immed(params, W, H, FOURCC_ARGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    close(fd);

    wl_surface_attach(top.surface, buffer, 0, 0);
    wl_surface_damage(top.surface, 0, 0, W, H);
    wl_surface_commit(top.surface);
    wl_display_flush(wl_dpy);
    printf("client_dmabuf_prime: committed dmabuf %dx%d\n", W, H);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("client_dmabuf_prime: disconnected\n");
    return 0;
}
