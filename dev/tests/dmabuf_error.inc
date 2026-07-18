#include "wl_util.h"

#include <linux-dmabuf-v1-client-protocol.h>

#define FOURCC_ARGB8888 0x34325241u

static struct zwp_linux_dmabuf_v1* dmabuf_test_global;
static uint32_t dmabuf_test_format;
static uint32_t dmabuf_test_mod_hi;
static uint32_t dmabuf_test_mod_lo;

static void dmabuf_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format) {
    (void)d; (void)z;
    if (!dmabuf_test_format) {
        dmabuf_test_format = format;
        dmabuf_test_mod_hi = 0xffffffffu;
        dmabuf_test_mod_lo = 0xffffffffu;
    }
}

static void dmabuf_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format,
                            uint32_t hi, uint32_t lo) {
    (void)d; (void)z;
    if (!dmabuf_test_format || format == FOURCC_ARGB8888) {
        dmabuf_test_format = format;
        dmabuf_test_mod_hi = hi;
        dmabuf_test_mod_lo = lo;
    }
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    dmabuf_format, dmabuf_modifier,
};

static void dmabuf_registry(void* d, struct wl_registry* registry, uint32_t name,
                            const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf_test_global = wl_registry_bind(registry, name,
                                              &zwp_linux_dmabuf_v1_interface, 3);
}

static void dmabuf_registry_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}

static const struct wl_registry_listener dmabuf_registry_listener = {
    dmabuf_registry, dmabuf_registry_remove,
};

static int dmabuf_test_boot(void) {
    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &dmabuf_registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dmabuf_test_global) return 77;

    zwp_linux_dmabuf_v1_add_listener(dmabuf_test_global, &dmabuf_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    return dmabuf_test_format ? 0 : 77;
}

static int dmabuf_test_fd(void) {
    int fd = memfd_create("not-a-dmabuf", 0);
    if (fd < 0 || ftruncate(fd, 4096) < 0) {
        perror("memfd");
        exit(2);
    }
    return fd;
}

static struct zwp_linux_buffer_params_v1* dmabuf_test_params(void) {
    return zwp_linux_dmabuf_v1_create_params(dmabuf_test_global);
}

static void dmabuf_test_add(struct zwp_linux_buffer_params_v1* params, uint32_t plane,
                            uint32_t offset, uint32_t stride, uint32_t hi, uint32_t lo) {
    int fd = dmabuf_test_fd();
    zwp_linux_buffer_params_v1_add(params, fd, plane, offset, stride, hi, lo);
    close(fd);
}
