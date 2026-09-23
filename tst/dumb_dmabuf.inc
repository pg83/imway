// dma-buf wl_buffers made from DRM dumb buffers exported through PRIME from
// a card node: no /dev/udmabuf and no GPU of the client's own needed. Include
// after wl_util.h; call dumb_boot() after wl_boot(). Every buffer has its own
// dma-buf, so the compositor sees a distinct fd per buffer.

#include <xf86drm.h>

#include <linux-dmabuf-v1-client-protocol.h>

#include <sys/mman.h>

static struct zwp_linux_dmabuf_v1* dumb_dmabuf_g;
static int dumb_linear_argb;

static void dumb_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t fmt) {
    (void)d;
    (void)z;
    (void)fmt;
}
static void dumb_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t fmt, uint32_t hi, uint32_t lo) {
    (void)d;
    (void)z;
    if (fmt == 0x34325241u && hi == 0 && lo == 0) dumb_linear_argb = 1;
}
static const struct zwp_linux_dmabuf_v1_listener dumb_dmabuf_listener = {dumb_format, dumb_modifier};

static void dumb_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name)) {
        dumb_dmabuf_g = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, 3);
        zwp_linux_dmabuf_v1_add_listener(dumb_dmabuf_g, &dumb_dmabuf_listener, NULL);
    }
}
static void dumb_global_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}
static const struct wl_registry_listener dumb_registry_listener = {dumb_global, dumb_global_remove};

// 0 when ARGB8888 + LINEAR dma-bufs can be offered, 77 (skip) otherwise
static int dumb_boot(void) {
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &dumb_registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dumb_dmabuf_g) return 77;
    wl_display_roundtrip(wl_dpy);
    return dumb_linear_argb ? 0 : 77;
}

// a w x h ARGB8888 LINEAR dma-buf filled with argb, or NULL without a card
// node that hands out dumb buffers
static struct wl_buffer* dumb_buffer(int w, int h, uint32_t argb) {
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        struct drm_mode_create_dumb create = {0};
        create.width = (uint32_t)w;
        create.height = (uint32_t)h;
        create.bpp = 32;
        struct drm_mode_map_dumb map = {0};
        int prime = -1;
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
            close(fd);
            continue;
        }
        map.handle = create.handle;
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0 ||
            drmPrimeHandleToFD(fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &prime) != 0 || prime < 0) {
            close(fd);
            continue;
        }

        uint32_t* px = mmap(NULL, (size_t)create.pitch * h, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
        if (px != MAP_FAILED) {
            for (size_t j = 0; j < (size_t)create.pitch * h / 4; j++) px[j] = argb;
            munmap(px, (size_t)create.pitch * h);
        }
        close(fd); /* the prime fd keeps the buffer alive */

        struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dumb_dmabuf_g);
        zwp_linux_buffer_params_v1_add(params, prime, 0, 0, create.pitch, 0, 0);
        close(prime);
        struct wl_buffer* buffer = zwp_linux_buffer_params_v1_create_immed(params, w, h, 0x34325241u, 0);
        zwp_linux_buffer_params_v1_destroy(params);
        return buffer;
    }
    return NULL;
}
