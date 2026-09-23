// A dma-buf the driver is asked to import through zwp_linux_buffer_params_v1
// .create (the asynchronous path). The scenario's IMWAY_CHAOS decides the
// driver's verdict on the first plane: argv[1] "refused" expects the params
// to report failed, "unjudged" (a card fd that cannot judge) expects the
// buffer to be created anyway. Exits 77 without a dma-buf source.
//   usage: client_dmabuf_prime_verdict refused|unjudged

#include "wl_util.h"

#include <linux-dmabuf-v1-client-protocol.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <drm_mode.h>

#define FOURCC_ARGB8888 0x34325241u

static struct zwp_linux_dmabuf_v1* dmabuf;
static int linear_argb, created, failed;

static void dma_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t f) { (void)d; (void)z; (void)f; }
static void dma_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t f, uint32_t hi, uint32_t lo) {
    (void)d; (void)z;
    if (f == FOURCC_ARGB8888 && hi == 0 && lo == 0) linear_argb = 1;
}
static const struct zwp_linux_dmabuf_v1_listener dma_listener = {dma_format, dma_modifier};

static void dma_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, 3);
}
static void dma_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener dma_reg_listener = {dma_global, dma_remove};

static struct wl_buffer* kept;

static void params_created(void* d, struct zwp_linux_buffer_params_v1* p, struct wl_buffer* b) {
    (void)d; (void)p;
    kept = b;
    created = 1;
}
static void params_failed(void* d, struct zwp_linux_buffer_params_v1* p) { (void)d; (void)p; failed = 1; }
static const struct zwp_linux_buffer_params_v1_listener params_listener = {params_created, params_failed};

// a 64x64 LINEAR ARGB8888 dma-buf: udmabuf, else a dumb buffer on card0
static int dmabuf_fd(uint32_t* stride) {
    int dev = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);

    if (dev >= 0) {
        int mem = memfd_create("dmabuf-prime", MFD_ALLOW_SEALING);

        if (mem >= 0 && ftruncate(mem, 64 * 64 * 4) == 0 && fcntl(mem, F_ADD_SEALS, F_SEAL_SHRINK) == 0) {
            struct udmabuf_create create = {0};

            create.memfd = mem;
            create.flags = UDMABUF_FLAGS_CLOEXEC;
            create.size = 64 * 64 * 4;

            int fd = ioctl(dev, UDMABUF_CREATE, &create);

            close(mem);
            close(dev);
            *stride = 64 * 4;
            if (fd >= 0) return fd;
        } else {
            if (mem >= 0) close(mem);
            close(dev);
        }
    }

    int card = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);

    if (card < 0) return -1;

    struct drm_mode_create_dumb create = {.height = 64, .width = 64, .bpp = 32};
    int fd = -1;

    if (drmIoctl(card, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0 ||
        drmPrimeHandleToFD(card, create.handle, DRM_CLOEXEC | DRM_RDWR, &fd) != 0) {
        fd = -1;
    }
    close(card);
    *stride = create.pitch;
    return fd;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (argc != 2) return 2;
    if (wl_boot()) return 1;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &dma_reg_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dmabuf) return 77;
    zwp_linux_dmabuf_v1_add_listener(dmabuf, &dma_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!linear_argb) return 77;

    uint32_t stride = 0;
    int fd = dmabuf_fd(&stride);

    if (fd < 0) return 77;

    struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);

    zwp_linux_buffer_params_v1_add_listener(params, &params_listener, NULL);
    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, stride, 0, 0);
    close(fd);
    zwp_linux_buffer_params_v1_create(params, 64, 64, FOURCC_ARGB8888, 0);
    while (!created && !failed && wl_display_dispatch(wl_dpy) != -1) {
    }

    int want_created = !strcmp(argv[1], "unjudged");

    printf("params %s\n", created ? "created" : failed ? "failed" : "lost");
    if (want_created ? !created : !failed) {
        fprintf(stderr, "%s: the params were %s\n", argv[1], created ? "created" : "failed");
        return 1;
    }
    return 0;
}
