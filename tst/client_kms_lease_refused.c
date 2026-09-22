/* wp-drm-lease when things go wrong. The scenario's IMWAY_CHAOS drops the
 * first connector offer and fails the first lease: the first device object
 * is offered nothing, a second one gets the connector; the refused lease
 * ends in finished with no fd, and destroying it revokes nothing; the retry
 * is granted. Releasing a device object answers released. Requesting the
 * same connector twice is a protocol error. */

#include "wl_util.h"

#include <drm-lease-v1-client-protocol.h>

struct device {
    struct wp_drm_lease_device_v1* dev;
    struct wp_drm_lease_connector_v1* connector;
    int done, released;
};

static void dev_drm_fd(void* d, struct wp_drm_lease_device_v1* dev, int32_t fd) {
    (void)d; (void)dev;
    close(fd);
}
static void dev_connector(void* d, struct wp_drm_lease_device_v1* dev,
                          struct wp_drm_lease_connector_v1* c) {
    (void)dev;
    struct device* device = d;

    if (device->connector) {
        wp_drm_lease_connector_v1_destroy(c);
        return;
    }

    device->connector = c;
}
static void dev_done(void* d, struct wp_drm_lease_device_v1* dev) {
    (void)dev;
    ((struct device*)d)->done = 1;
}
static void dev_released(void* d, struct wp_drm_lease_device_v1* dev) {
    (void)dev;
    ((struct device*)d)->released = 1;
}
static const struct wp_drm_lease_device_v1_listener dev_listener = {
    dev_drm_fd, dev_connector, dev_done, dev_released,
};

static uint32_t device_name;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)r; (void)ver;
    if (!strcmp(iface, wp_drm_lease_device_v1_interface.name))
        device_name = name;
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_registry* registry;

static void bind_device(struct device* device) {
    memset(device, 0, sizeof(*device));
    device->dev = wl_registry_bind(registry, device_name, &wp_drm_lease_device_v1_interface, 1);
    wp_drm_lease_device_v1_add_listener(device->dev, &dev_listener, device);

    while (!device->done && wl_display_dispatch(wl_dpy) != -1) {
    }
}

static int lease_fd = -1, lease_finished;

static void lease_lease_fd(void* d, struct wp_drm_lease_v1* l, int32_t fd) {
    (void)d; (void)l;
    lease_fd = fd;
}
static void lease_finished_cb(void* d, struct wp_drm_lease_v1* l) {
    (void)d; (void)l;
    lease_finished = 1;
}
static const struct wp_drm_lease_v1_listener lease_listener = {lease_lease_fd, lease_finished_cb};

static struct wp_drm_lease_v1* lease(struct device* device) {
    struct wp_drm_lease_request_v1* req = wp_drm_lease_device_v1_create_lease_request(device->dev);

    wp_drm_lease_request_v1_request_connector(req, device->connector);

    struct wp_drm_lease_v1* l = wp_drm_lease_request_v1_submit(req);

    lease_fd = -1;
    lease_finished = 0;
    wp_drm_lease_v1_add_listener(l, &lease_listener, NULL);

    while (lease_fd < 0 && !lease_finished && wl_display_dispatch(wl_dpy) != -1) {
    }

    return l;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!device_name) {
        fprintf(stderr, "no wp_drm_lease_device_v1\n");
        return 1;
    }

    struct device first, second;

    bind_device(&first);
    if (first.connector) {
        fprintf(stderr, "the dropped connector offer arrived anyway\n");
        return 1;
    }

    bind_device(&second);
    if (!second.connector) {
        fprintf(stderr, "the second device object was offered no connector\n");
        return 1;
    }

    puts("offer dropped once");

    struct wp_drm_lease_v1* refused = lease(&second);

    if (!lease_finished || lease_fd >= 0) {
        fprintf(stderr, "the failed lease was not finished (fd %d)\n", lease_fd);
        return 1;
    }

    wp_drm_lease_v1_destroy(refused);
    wl_display_roundtrip(wl_dpy);
    puts("lease refused");

    struct wp_drm_lease_v1* granted = lease(&second);

    if (lease_fd < 0 || lease_finished) {
        fprintf(stderr, "the retry was not granted\n");
        return 1;
    }

    close(lease_fd);
    wp_drm_lease_v1_destroy(granted);
    puts("retry granted");

    wp_drm_lease_device_v1_release(first.dev);

    while (!first.released && wl_display_dispatch(wl_dpy) != -1) {
    }

    puts("device released");

    struct wp_drm_lease_request_v1* dup = wp_drm_lease_device_v1_create_lease_request(second.dev);

    wp_drm_lease_request_v1_request_connector(dup, second.connector);
    wp_drm_lease_request_v1_request_connector(dup, second.connector);

    if (wl_expect_error(wp_drm_lease_request_v1_interface.name,
                        WP_DRM_LEASE_REQUEST_V1_ERROR_DUPLICATE_CONNECTOR))
        return 1;

    puts("lease refusals done");

    return 0;
}
