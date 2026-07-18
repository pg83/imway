/* PLAN buffer lifetime #12: one dmabuf wl_buffer used by the parent and a
 * sync subsurface at the same time, then destroyed while both still show it.
 * Exits 77 when udmabuf/dmabuf is unavailable. */
#include "wl_util.h"

#include <linux-dmabuf-v1-client-protocol.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>

#define FOURCC_ARGB8888 0x34325241u

static struct zwp_linux_dmabuf_v1* dmabuf;
static int linear_argb;

static void dma_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format) {
    (void)d; (void)z; (void)format;
}
static void dma_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format, uint32_t hi,
                         uint32_t lo) {
    (void)d; (void)z;
    if (format == FOURCC_ARGB8888 && hi == 0 && lo == 0) linear_argb = 1;
}
static const struct zwp_linux_dmabuf_v1_listener dma_listener = {dma_format, dma_modifier};

static void dma_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                       uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, 3);
}
static void dma_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener dma_reg_listener = {dma_global, dma_remove};

static int make_dmabuf_fd(size_t size) {
    int dev = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
    if (dev < 0) exit(77);
    int mem = memfd_create("dmabuf-shared", MFD_ALLOW_SEALING);
    if (mem < 0 || ftruncate(mem, size) < 0 || fcntl(mem, F_ADD_SEALS, F_SEAL_SHRINK) < 0)
        exit(77);
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mem, 0);
    for (size_t i = 0; i < size / 4; i++) px[i] = 0xFFFF8000;
    munmap(px, size);
    struct udmabuf_create create = {0};
    create.memfd = mem;
    create.flags = UDMABUF_FLAGS_CLOEXEC;
    create.size = size;
    int fd = ioctl(dev, UDMABUF_CREATE, &create);
    close(mem);
    close(dev);
    if (fd < 0) exit(77);
    return fd;
}

int main(void) {
    alarm(10);
    if (wl_boot() || !wl_subcomp) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &dma_reg_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dmabuf) return 77;
    zwp_linux_dmabuf_v1_add_listener(dmabuf, &dma_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!linear_argb) return 77;

    long page = sysconf(_SC_PAGESIZE);
    size_t size = ((size_t)64 * 64 * 4 + page - 1) / page * page;
    int fd = make_dmabuf_fd(size);
    struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, 64 * 4, 0, 0);
    struct wl_buffer* shared =
        zwp_linux_buffer_params_v1_create_immed(params, 64, 64, FOURCC_ARGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    close(fd);

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "dmabuf-shared", 128, 128, 0xffff0000);
    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    wl_subsurface_set_sync(sub);
    wl_subsurface_set_position(sub, 32, 32);

    wl_surface_attach(child, shared, 0, 0);
    wl_surface_damage(child, 0, 0, 64, 64);
    wl_surface_commit(child);
    wl_surface_attach(top.surface, shared, 0, 0);
    wl_surface_damage(top.surface, 0, 0, 64, 64);
    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    wl_buffer_destroy(shared); /* storage must live on inside the compositor */
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    usleep(100000);
    return wl_display_roundtrip(wl_dpy) < 0 ? 1 : 0;
}
