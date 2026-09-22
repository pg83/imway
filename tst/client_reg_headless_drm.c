/* The drm identity a headless compositor presents, whichever nodes its
 * host has. With "none" as the argument the host has no drm node at all:
 * no lease device and no explicit sync; dmabuf feedback exists only when
 * the renderer names its own device (a GPU driver does, a software one
 * does not), never with an empty main device. With a node path the compositor
 * picked that node: the lease device offers its fd and no connectors, and
 * the dmabuf feedback's main device is the node's own device number. */

#include "wl_util.h"

#include <drm-lease-v1-client-protocol.h>
#include <linux-dmabuf-v1-client-protocol.h>

#include <sys/stat.h>

static struct zwp_linux_dmabuf_v1* dmabuf;
static uint32_t dmabuf_version;
static struct wp_drm_lease_device_v1* lease_dev;
static int syncobj;
static int lease_fd = -1;
static int lease_connectors;
static int lease_done;
static dev_t main_device;
static int feedback_done;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name)) {
        dmabuf_version = v;
        dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, v < 4 ? v : 4);
    } else if (!strcmp(iface, wp_drm_lease_device_v1_interface.name)) {
        lease_dev = wl_registry_bind(r, name, &wp_drm_lease_device_v1_interface, 1);
    } else if (!strcmp(iface, "wp_linux_drm_syncobj_manager_v1")) {
        syncobj = 1;
    }
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void dev_drm_fd(void* d, struct wp_drm_lease_device_v1* dev, int32_t fd) {
    (void)d; (void)dev;
    lease_fd = fd;
}
static void dev_connector(void* d, struct wp_drm_lease_device_v1* dev,
                          struct wp_drm_lease_connector_v1* c) {
    (void)d; (void)dev;
    lease_connectors++;
    wp_drm_lease_connector_v1_destroy(c);
}
static void dev_done(void* d, struct wp_drm_lease_device_v1* dev) {
    (void)d; (void)dev;
    lease_done = 1;
}
static void dev_released(void* d, struct wp_drm_lease_device_v1* dev) {
    (void)d; (void)dev;
}
static const struct wp_drm_lease_device_v1_listener dev_listener = {
    dev_drm_fd, dev_connector, dev_done, dev_released,
};

static void fb_done(void* d, struct zwp_linux_dmabuf_feedback_v1* f) {
    (void)d; (void)f;
    feedback_done = 1;
}
static void fb_format_table(void* d, struct zwp_linux_dmabuf_feedback_v1* f, int32_t fd, uint32_t size) {
    (void)d; (void)f; (void)size;
    close(fd);
}
static void fb_main_device(void* d, struct zwp_linux_dmabuf_feedback_v1* f, struct wl_array* dev) {
    (void)d; (void)f;
    if (dev->size == sizeof(dev_t)) {
        memcpy(&main_device, dev->data, sizeof(dev_t));
    }
}
static void fb_tranche_done(void* d, struct zwp_linux_dmabuf_feedback_v1* f) { (void)d; (void)f; }
static void fb_tranche_target_device(void* d, struct zwp_linux_dmabuf_feedback_v1* f, struct wl_array* dev) {
    (void)d; (void)f; (void)dev;
}
static void fb_tranche_formats(void* d, struct zwp_linux_dmabuf_feedback_v1* f, struct wl_array* idx) {
    (void)d; (void)f; (void)idx;
}
static void fb_tranche_flags(void* d, struct zwp_linux_dmabuf_feedback_v1* f, uint32_t flags) {
    (void)d; (void)f; (void)flags;
}
static const struct zwp_linux_dmabuf_feedback_v1_listener fb_listener = {
    fb_done, fb_format_table, fb_main_device, fb_tranche_done,
    fb_tranche_target_device, fb_tranche_formats, fb_tranche_flags,
};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (argc != 2) {
        fprintf(stderr, "usage: %s none|<node>\n", argv[0]);
        return 2;
    }

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!dmabuf) {
        fprintf(stderr, "no linux-dmabuf global\n");
        return 1;
    }

    if (!strcmp(argv[1], "none")) {
        if (lease_dev || syncobj) {
            fprintf(stderr, "a drm-backed global without a drm node (lease %d, syncobj %d)\n", lease_dev != NULL, syncobj);
            return 1;
        }
        if (dmabuf_version >= 4) {
            struct zwp_linux_dmabuf_feedback_v1* fb = zwp_linux_dmabuf_v1_get_default_feedback(dmabuf);

            zwp_linux_dmabuf_feedback_v1_add_listener(fb, &fb_listener, NULL);
            wl_display_roundtrip(wl_dpy);

            if (!feedback_done || !main_device) {
                fprintf(stderr, "linux-dmabuf v%u feedback without a main device\n", dmabuf_version);
                return 1;
            }

            zwp_linux_dmabuf_feedback_v1_destroy(fb);
        }
        printf("no drm node: dmabuf v%u, no lease, no syncobj\n", dmabuf_version);
        return 0;
    }

    struct stat st;

    if (stat(argv[1], &st) != 0) {
        fprintf(stderr, "cannot stat %s\n", argv[1]);
        return 1;
    }

    if (!lease_dev) {
        fprintf(stderr, "no lease device on a drm node\n");
        return 1;
    }

    wp_drm_lease_device_v1_add_listener(lease_dev, &dev_listener, NULL);

    if (dmabuf_version < 4) {
        fprintf(stderr, "linux-dmabuf v%u: no feedback on a drm node\n", dmabuf_version);
        return 1;
    }

    struct zwp_linux_dmabuf_feedback_v1* fb = zwp_linux_dmabuf_v1_get_default_feedback(dmabuf);

    zwp_linux_dmabuf_feedback_v1_add_listener(fb, &fb_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (!lease_done || lease_fd < 0 || lease_connectors) {
        fprintf(stderr, "lease device: done %d, fd %d, connectors %d\n", lease_done, lease_fd, lease_connectors);
        return 1;
    }

    if (!feedback_done || main_device != st.st_rdev) {
        fprintf(stderr, "feedback done %d, main device %lx, node %lx\n", feedback_done,
                (unsigned long)main_device, (unsigned long)st.st_rdev);
        return 1;
    }

    close(lease_fd);
    zwp_linux_dmabuf_feedback_v1_destroy(fb);
    printf("drm node %s: lease device without connectors, main device matches\n", argv[1]);
    return 0;
}
