/* wp-drm-lease when the kernel side cannot give the connector away: the
 * scenario breaks the lease path one way at a time (the connector unplugged,
 * its encoder gone, no crtc left for it, the kernel refusing the lease, the
 * driver failing to list its resources) and signals each through a marker
 * file. Every one of those requests must end in `finished` without an fd,
 * and a device bound while the connector or the resource list is gone
 * offers no connector. With the lease crtc's plane unreadable the lease
 * still goes out, without it; healed, the same connector leases out. */

#include "wl_util.h"

#include <drm-lease-v1-client-protocol.h>

#include <sys/stat.h>

static struct wp_drm_lease_device_v1* lease_dev;
static struct wl_registry* registry;
static uint32_t lease_name;
static int fresh_connectors;
static int fresh_done;
static struct wp_drm_lease_connector_v1* offered;
static int device_done;
static int lease_fd = -1;
static int lease_finished;

static void dev_drm_fd(void* d, struct wp_drm_lease_device_v1* dev, int32_t fd) {
    (void)d; (void)dev;
    close(fd);
}

static void conn_name(void* d, struct wp_drm_lease_connector_v1* c, const char* name) {
    (void)d; (void)c; (void)name;
}
static void conn_description(void* d, struct wp_drm_lease_connector_v1* c, const char* text) {
    (void)d; (void)c; (void)text;
}
static void conn_connector_id(void* d, struct wp_drm_lease_connector_v1* c, uint32_t id) {
    (void)d; (void)c; (void)id;
}
static void conn_done(void* d, struct wp_drm_lease_connector_v1* c) {
    (void)d; (void)c;
}
static void conn_withdrawn(void* d, struct wp_drm_lease_connector_v1* c) {
    (void)d; (void)c;
}
static const struct wp_drm_lease_connector_v1_listener conn_listener = {
    conn_name, conn_description, conn_connector_id, conn_done, conn_withdrawn,
};

static void dev_connector(void* d, struct wp_drm_lease_device_v1* dev,
                          struct wp_drm_lease_connector_v1* c) {
    (void)d; (void)dev;
    if (offered) {
        wp_drm_lease_connector_v1_destroy(c);
        return;
    }
    offered = c;
    wp_drm_lease_connector_v1_add_listener(c, &conn_listener, NULL);
}

static void dev_done(void* d, struct wp_drm_lease_device_v1* dev) {
    (void)d; (void)dev;
    device_done = 1;
}

static void dev_released(void* d, struct wp_drm_lease_device_v1* dev) {
    (void)d; (void)dev;
}

static const struct wp_drm_lease_device_v1_listener dev_listener = {
    dev_drm_fd, dev_connector, dev_done, dev_released,
};

static void lease_lease_fd(void* d, struct wp_drm_lease_v1* l, int32_t fd) {
    (void)d; (void)l;
    lease_fd = fd;
}
static void lease_finished_cb(void* d, struct wp_drm_lease_v1* l) {
    (void)d; (void)l;
    lease_finished = 1;
}
static const struct wp_drm_lease_v1_listener lease_listener = {
    lease_lease_fd, lease_finished_cb,
};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, wp_drm_lease_device_v1_interface.name)) {
        lease_name = name;
        lease_dev = wl_registry_bind(r, name, &wp_drm_lease_device_v1_interface, 1);
        wp_drm_lease_device_v1_add_listener(lease_dev, &dev_listener, NULL);
    }
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void fresh_drm_fd(void* d, struct wp_drm_lease_device_v1* dev, int32_t fd) {
    (void)d; (void)dev;
    close(fd);
}
static void fresh_connector(void* d, struct wp_drm_lease_device_v1* dev,
                            struct wp_drm_lease_connector_v1* c) {
    (void)d; (void)dev;
    fresh_connectors++;
    wp_drm_lease_connector_v1_destroy(c);
}
static void fresh_device_done(void* d, struct wp_drm_lease_device_v1* dev) {
    (void)d; (void)dev;
    fresh_done = 1;
}
static const struct wp_drm_lease_device_v1_listener fresh_listener = {
    fresh_drm_fd, fresh_connector, fresh_device_done, dev_released,
};

/* a second lease device bound now: how many connectors it offers, -1 when
 * it never says done */
static int fresh_bind(void) {
    struct wp_drm_lease_device_v1* dev = wl_registry_bind(registry, lease_name, &wp_drm_lease_device_v1_interface, 1);

    fresh_connectors = 0;
    fresh_done = 0;
    wp_drm_lease_device_v1_add_listener(dev, &fresh_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
    wp_drm_lease_device_v1_destroy(dev);

    return fresh_done ? fresh_connectors : -1;
}

static void wait_marker(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", getenv("XDG_RUNTIME_DIR"), name);
    while (access(path, F_OK) != 0) {
        wl_display_dispatch_pending(wl_dpy);
        wl_display_flush(wl_dpy);
        usleep(20000);
    }
}

/* one lease request for the offered connector; 1 when it came back with an
 * fd, 0 when it was finished without one */
static int try_lease(void) {
    struct wp_drm_lease_request_v1* req = wp_drm_lease_device_v1_create_lease_request(lease_dev);

    wp_drm_lease_request_v1_request_connector(req, offered);

    struct wp_drm_lease_v1* lease = wp_drm_lease_request_v1_submit(req);

    lease_fd = -1;
    lease_finished = 0;
    wp_drm_lease_v1_add_listener(lease, &lease_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
    wp_drm_lease_v1_destroy(lease);

    if (lease_fd >= 0) {
        close(lease_fd);
        return 1;
    }

    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 2;

    registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!lease_dev) {
        fprintf(stderr, "no wp_drm_lease_device_v1\n");
        return 1;
    }

    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (!device_done || !offered) {
        fprintf(stderr, "no connector offered for lease\n");
        return 1;
    }

    puts("offered");

    for (int kind = 1; kind <= 5; kind++) {
        char marker[32];

        snprintf(marker, sizeof(marker), "go-%d", kind);
        wait_marker(marker);

        // the connector gone, or no resource list: nothing to offer
        if ((kind == 1 || kind == 5) && fresh_bind() != 0) {
            fprintf(stderr, "lease fault %d: a fresh device offered %d connectors\n", kind, fresh_connectors);
            return 1;
        }

        if (try_lease()) {
            fprintf(stderr, "lease fault %d still handed out an fd\n", kind);
            return 1;
        }

        if (!lease_finished) {
            fprintf(stderr, "lease fault %d: neither fd nor finished\n", kind);
            return 1;
        }

        printf("refused %d\n", kind);
    }

    wait_marker("go-6");

    if (!try_lease()) {
        fprintf(stderr, "an unreadable plane refused the whole lease\n");
        return 1;
    }

    puts("leased without the plane");

    wait_marker("go-7");

    if (!try_lease()) {
        fprintf(stderr, "an unreadable plane list refused the whole lease\n");
        return 1;
    }

    puts("leased without planes");

    wait_marker("go-0");

    if (!try_lease()) {
        fprintf(stderr, "the healed lease path still refuses\n");
        return 1;
    }

    puts("leased");

    wp_drm_lease_connector_v1_destroy(offered);
    wp_drm_lease_device_v1_destroy(lease_dev);
    wl_registry_destroy(registry);

    return 0;
}
