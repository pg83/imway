// dmabuf buffers moving through a sync subsurface's cache, checked through
// the release events the client gets:
//   1. a surface that showed an shm buffer switches to a dmabuf: the shm
//      buffer is released
//   2. a sync child caches dmabuf A, then commits dmabuf B before the parent
//      commits: A is released at once, with its get_release callback
//   3. a cached dmabuf's wl_buffer is destroyed before the parent commit;
//      the parent commit still applies it (the compositor holds the storage)
//   4. the child caches dmabuf C and the subsurface is destroyed with it
//      cached: C is released
//   5. a dmabuf shown directly, committed with a get_release callback, is
//      replaced: it is released, and its callback fires with the release
// Buffers come from /dev/udmabuf, else a dumb buffer on /dev/dri/card0;
// exits 77 when neither is there.

#define REG_COMPOSITOR_VERSION 7
#include "wl_util.h"

#include <linux-dmabuf-v1-client-protocol.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <drm_mode.h>

#define FOURCC_ARGB8888 0x34325241u
#define W 64
#define H 64

static struct zwp_linux_dmabuf_v1* dmabuf;
static int linear_argb;

static void dma_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format) {
    (void)d; (void)z; (void)format;
}
static void dma_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format, uint32_t hi, uint32_t lo) {
    (void)d; (void)z;
    if (format == FOURCC_ARGB8888 && hi == 0 && lo == 0) linear_argb = 1;
}
static const struct zwp_linux_dmabuf_v1_listener dma_listener = {dma_format, dma_modifier};

static void dma_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, 3);
}
static void dma_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener dma_reg_listener = {dma_global, dma_remove};

// a LINEAR ARGB8888 dmabuf of one colour: fd and stride
static int udmabuf_fd(uint32_t color, uint32_t* stride) {
    int dev = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);

    if (dev < 0) return -1;

    long page = sysconf(_SC_PAGESIZE);
    size_t size = ((size_t)W * H * 4 + page - 1) / page * page;
    int mem = memfd_create("dmabuf-cache", MFD_ALLOW_SEALING);

    if (mem < 0 || ftruncate(mem, size) < 0 || fcntl(mem, F_ADD_SEALS, F_SEAL_SHRINK) < 0) exit(1);

    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mem, 0);

    for (size_t i = 0; i < size / 4; i++) px[i] = color;
    munmap(px, size);

    struct udmabuf_create create = {0};

    create.memfd = mem;
    create.flags = UDMABUF_FLAGS_CLOEXEC;
    create.size = size;

    int fd = ioctl(dev, UDMABUF_CREATE, &create);

    close(mem);
    close(dev);
    *stride = W * 4;
    return fd;
}

static int dumb_fd(uint32_t color, uint32_t* stride) {
    int card = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);

    if (card < 0) return -1;

    struct drm_mode_create_dumb create = {.height = H, .width = W, .bpp = 32};

    if (drmIoctl(card, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        close(card);
        return -1;
    }

    struct drm_mode_map_dumb map = {.handle = create.handle};

    if (drmIoctl(card, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        close(card);
        return -1;
    }

    uint32_t* px = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, card, (off_t)map.offset);

    if (px == MAP_FAILED) {
        close(card);
        return -1;
    }
    for (size_t i = 0; i < create.size / 4; i++) px[i] = color;
    munmap(px, create.size);

    int fd = -1;

    if (drmPrimeHandleToFD(card, create.handle, DRM_CLOEXEC | DRM_RDWR, &fd) < 0) fd = -1;
    // the prime fd keeps the buffer alive once the card is gone
    close(card);
    *stride = create.pitch;
    return fd;
}

static struct wl_buffer* make_buffer(uint32_t color) {
    uint32_t stride = 0;
    int fd = udmabuf_fd(color, &stride);

    if (fd < 0) fd = dumb_fd(color, &stride);
    if (fd < 0) exit(77);

    struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);

    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, stride, 0, 0);

    struct wl_buffer* buf = zwp_linux_buffer_params_v1_create_immed(params, W, H, FOURCC_ARGB8888, 0);

    zwp_linux_buffer_params_v1_destroy(params);
    close(fd);
    return buf;
}

// per-buffer release counts, by the index given as listener data
static int released[8];

static void buffer_release(void* d, struct wl_buffer* b) {
    (void)b;
    released[(intptr_t)d]++;
}
static const struct wl_buffer_listener buffer_listener = {buffer_release};

static struct wl_buffer* tracked(struct wl_buffer* b, intptr_t index) {
    wl_buffer_add_listener(b, &buffer_listener, (void*)index);
    return b;
}

static int release_cb_done;

static void release_cb(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d; (void)cb; (void)t;
    release_cb_done++;
}
static const struct wl_callback_listener release_cb_listener = {release_cb};

static int settle(int index) {
    for (int i = 0; i < 100 && !released[index]; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 0;
        usleep(10000);
    }
    return released[index];
}

static void attach(struct wl_surface* s, struct wl_buffer* b) {
    wl_surface_attach(s, b, 0, 0);
    wl_surface_damage(s, 0, 0, W, H);
    wl_surface_commit(s);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_subcomp) return 1;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &dma_reg_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dmabuf) return 77;
    zwp_linux_dmabuf_v1_add_listener(dmabuf, &dma_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!linear_argb) return 77;

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "dmabuf-cache", 160, 160, 0xffff0000);

    // 1: shm to dmabuf on one (desync) surface
    struct wl_surface* plain = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* plain_sub = wl_subcompositor_get_subsurface(wl_subcomp, plain, top.surface);

    wl_subsurface_set_desync(plain_sub);
    attach(plain, tracked(wl_solid(W, H, 0xff00ff00), 0));
    wl_display_roundtrip(wl_dpy);
    usleep(50000);
    wl_display_roundtrip(wl_dpy);
    attach(plain, make_buffer(0xff0000ff));
    if (!settle(0)) {
        fprintf(stderr, "the shm buffer a dmabuf replaced was not released\n");
        return 1;
    }

    // 2: a cached dmabuf replaced before the parent commit
    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);

    wl_subsurface_set_position(sub, 80, 80);
    wl_surface_attach(child, tracked(make_buffer(0xffff8000), 1), 0, 0);
    wl_callback_add_listener(wl_surface_get_release(child), &release_cb_listener, NULL);
    wl_surface_commit(child);
    attach(child, tracked(make_buffer(0xff8000ff), 2));
    if (!settle(1) || release_cb_done != 1) {
        fprintf(stderr, "the replaced cached dmabuf: released=%d release callback=%d\n", released[1], release_cb_done);
        return 1;
    }

    // 3: the cached dmabuf's wl_buffer destroyed before the parent commits
    struct wl_buffer* b3 = tracked(make_buffer(0xff00ffff), 3);

    attach(child, b3);
    wl_display_roundtrip(wl_dpy);
    wl_buffer_destroy(b3);
    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "applying a cached dmabuf whose wl_buffer is gone failed\n");
        return 1;
    }

    // 4: a subsurface destroyed with a dmabuf in its cache
    attach(child, tracked(make_buffer(0xffffff00), 4));
    wl_display_roundtrip(wl_dpy);
    wl_subsurface_destroy(sub);
    if (!settle(4)) {
        fprintf(stderr, "the dmabuf cached by a destroyed subsurface was not released\n");
        return 1;
    }

    // 5: a directly shown dmabuf's release callback
    wl_surface_attach(plain, tracked(make_buffer(0xff804020), 5), 0, 0);
    wl_callback_add_listener(wl_surface_get_release(plain), &release_cb_listener, NULL);
    wl_surface_damage(plain, 0, 0, W, H);
    wl_surface_commit(plain);
    wl_display_roundtrip(wl_dpy);
    usleep(50000);
    wl_display_roundtrip(wl_dpy);
    attach(plain, tracked(wl_solid(W, H, 0xff00ff00), 6));
    if (!settle(5) || release_cb_done != 2) {
        fprintf(stderr, "the replaced dmabuf: released=%d release callbacks=%d\n", released[5], release_cb_done);
        return 1;
    }

    printf("dmabuf cache ok\n");
    return 0;
}
