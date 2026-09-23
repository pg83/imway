/* wp-drm-lease objects that fail to allocate. The scenario's IMWAY_CHAOS
 * fails the first lease device, lease request and lease objects; each run
 * walks its chain on a fresh connection and must get the wl_display
 * no_memory error:
 *   device   — bind the lease device
 *   request  — bind it, ask for a lease request
 *   lease    — bind it, request the offered connector, submit */

#include "wl_util.h"

#include <drm-lease-v1-client-protocol.h>

static uint32_t device_name;
static struct wp_drm_lease_device_v1* device;
static struct wp_drm_lease_connector_v1* connector;
static int device_done;

static void dev_drm_fd(void* d, struct wp_drm_lease_device_v1* dev, int32_t fd) {
    (void)d; (void)dev;
    close(fd);
}
static void dev_connector(void* d, struct wp_drm_lease_device_v1* dev, struct wp_drm_lease_connector_v1* c) {
    (void)d; (void)dev;
    if (!connector)
        connector = c;
}
static void dev_done(void* d, struct wp_drm_lease_device_v1* dev) {
    (void)d; (void)dev;
    device_done = 1;
}
static void dev_released(void* d, struct wp_drm_lease_device_v1* dev) { (void)d; (void)dev; }
static const struct wp_drm_lease_device_v1_listener dev_listener = {dev_drm_fd, dev_connector, dev_done, dev_released};

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)r; (void)v;
    if (!strcmp(iface, wp_drm_lease_device_v1_interface.name))
        device_name = name;
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_registry* registry;

static int expect_no_memory(const char* what) {
    if (wl_display_roundtrip(wl_dpy) >= 0) {
        fprintf(stderr, "%s: the request went through\n", what);
        return 1;
    }

    if (wl_display_get_error(wl_dpy) != ENOMEM) {
        fprintf(stderr, "%s: wrong error %d\n", what, wl_display_get_error(wl_dpy));
        return 1;
    }

    printf("%s: no_memory\n", what);

    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (argc < 2 || wl_boot()) return 2;

    registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!device_name) {
        fprintf(stderr, "no wp_drm_lease_device_v1\n");
        return 2;
    }

    device = wl_registry_bind(registry, device_name, &wp_drm_lease_device_v1_interface, 1);

    if (!strcmp(argv[1], "device"))
        return expect_no_memory("device");

    wp_drm_lease_device_v1_add_listener(device, &dev_listener, NULL);

    while (!device_done && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wp_drm_lease_request_v1* request = wp_drm_lease_device_v1_create_lease_request(device);

    if (!strcmp(argv[1], "request"))
        return expect_no_memory("request");

    if (!connector) {
        fprintf(stderr, "no connector offered\n");
        return 1;
    }

    wp_drm_lease_request_v1_request_connector(request, connector);
    wp_drm_lease_request_v1_submit(request);

    return expect_no_memory("lease");
}
