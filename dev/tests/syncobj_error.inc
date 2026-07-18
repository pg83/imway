#include "wl_util.h"

#include <linux-dmabuf-v1-client-protocol.h>
#include <linux-drm-syncobj-v1-client-protocol.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <xf86drm.h>

static struct wp_linux_drm_syncobj_manager_v1* sync_manager;
static struct zwp_linux_dmabuf_v1* sync_dmabuf;
static int sync_linear_argb;

static void sync_dmabuf_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format) {
    (void)d; (void)z; (void)format;
}
static void sync_dmabuf_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format,
                                 uint32_t hi, uint32_t lo) {
    (void)d; (void)z;
    if (format == 0x34325241u && hi == 0 && lo == 0) sync_linear_argb = 1;
}
static const struct zwp_linux_dmabuf_v1_listener sync_dmabuf_listener = {
    sync_dmabuf_format, sync_dmabuf_modifier,
};

static void sync_registry(void* d, struct wl_registry* registry, uint32_t name,
                          const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_linux_drm_syncobj_manager_v1_interface.name))
        sync_manager = wl_registry_bind(
            registry, name, &wp_linux_drm_syncobj_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        sync_dmabuf = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, 3);
}
static void sync_registry_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener sync_registry_listener = {
    sync_registry, sync_registry_remove,
};

static int sync_test_boot(void) {
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &sync_registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (sync_dmabuf) {
        zwp_linux_dmabuf_v1_add_listener(sync_dmabuf, &sync_dmabuf_listener, NULL);
        wl_display_roundtrip(wl_dpy);
    }
    return sync_manager ? 0 : 77;
}

static int sync_test_drm_fd(void) {
    char path[64];
    for (int i = 128; i < 136; i++) {
        snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        uint64_t cap = 0;
        if (drmGetCap(fd, DRM_CAP_SYNCOBJ_TIMELINE, &cap) == 0 && cap) return fd;
        close(fd);
    }
    return -1;
}

/* the drm fd + syncobj handle behind the last sync_test_timeline_keep(),
 * so a test can signal points after the protocol object is gone */
static int sync_signal_fd = -1;
static uint32_t sync_signal_handle;

static struct wp_linux_drm_syncobj_timeline_v1* sync_test_timeline_impl(int keep) {
    int drm_fd = sync_test_drm_fd();
    if (drm_fd < 0) return NULL;
    uint32_t handle = 0;
    int timeline_fd = -1;
    if (drmSyncobjCreate(drm_fd, 0, &handle) != 0 ||
        drmSyncobjHandleToFD(drm_fd, handle, &timeline_fd) != 0) {
        if (handle) drmSyncobjDestroy(drm_fd, handle);
        close(drm_fd);
        return NULL;
    }
    if (keep) {
        sync_signal_fd = drm_fd;
        sync_signal_handle = handle;
    } else {
        drmSyncobjDestroy(drm_fd, handle);
        close(drm_fd);
    }
    struct wp_linux_drm_syncobj_timeline_v1* timeline =
        wp_linux_drm_syncobj_manager_v1_import_timeline(sync_manager, timeline_fd);
    close(timeline_fd);
    return timeline;
}

static struct wp_linux_drm_syncobj_timeline_v1* sync_test_timeline(void) {
    return sync_test_timeline_impl(0);
}

__attribute__((unused))
static struct wp_linux_drm_syncobj_timeline_v1* sync_test_timeline_keep(void) {
    return sync_test_timeline_impl(1);
}

static struct wl_buffer* sync_test_dmabuf(void) {
    if (!sync_dmabuf || !sync_linear_argb) return NULL;
    int dev = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
    int mem = memfd_create("syncobj-dmabuf", MFD_ALLOW_SEALING);
    if (dev < 0 || mem < 0 || ftruncate(mem, 4096) < 0 ||
        fcntl(mem, F_ADD_SEALS, F_SEAL_SHRINK) < 0) {
        if (dev >= 0) close(dev);
        if (mem >= 0) close(mem);
        return NULL;
    }
    struct udmabuf_create create = {0};
    create.memfd = mem;
    create.flags = UDMABUF_FLAGS_CLOEXEC;
    create.size = 4096;
    int fd = ioctl(dev, UDMABUF_CREATE, &create);
    close(mem);
    close(dev);
    if (fd < 0) return NULL;

    struct zwp_linux_buffer_params_v1* params =
        zwp_linux_dmabuf_v1_create_params(sync_dmabuf);
    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, 4, 0, 0);
    close(fd);
    return zwp_linux_buffer_params_v1_create_immed(params, 1, 1, 0x34325241u, 0);
}
