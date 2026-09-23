/* wp-drm-lease: the compositor offers the non-desktop connector of the KMS
 * emulator, never the one it drives. This client takes the lease, checks the
 * fd it receives is a live device, drops it, then proves the error paths: an
 * empty lease request is a protocol error, and so is requesting the same
 * connector twice. */

#include "wl_util.h"

#include <drm-lease-v1-client-protocol.h>

#include <sys/stat.h>

static struct wp_drm_lease_device_v1* lease_dev;
static struct wp_drm_lease_connector_v1* offered;
static char offered_name[128];
static uint32_t offered_id;
static int device_drm_fd = -1;
static int device_done;
static int lease_fd = -1;
static int lease_finished;

static void dev_drm_fd(void* d, struct wp_drm_lease_device_v1* dev, int32_t fd) {
    (void)d; (void)dev;
    device_drm_fd = fd;
}

static void conn_name(void* d, struct wp_drm_lease_connector_v1* c, const char* name) {
    (void)d; (void)c;
    snprintf(offered_name, sizeof(offered_name), "%s", name);
}
static void conn_description(void* d, struct wp_drm_lease_connector_v1* c, const char* text) {
    (void)d; (void)c; (void)text;
}
static void conn_connector_id(void* d, struct wp_drm_lease_connector_v1* c, uint32_t id) {
    (void)d; (void)c;
    offered_id = id;
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

/* the throwaway connection below binds its own device: whatever the
 * compositor hands it has to be released too, or a sanitized run counts the
 * proxies and the duplicated drm fd as leaks */
static void spare_drm_fd(void* d, struct wp_drm_lease_device_v1* dev, int32_t fd) {
    (void)d; (void)dev;
    close(fd);
}
static void spare_connector(void* d, struct wp_drm_lease_device_v1* dev,
                            struct wp_drm_lease_connector_v1* c) {
    (void)d; (void)dev;
    wp_drm_lease_connector_v1_destroy(c);
}
static void spare_done(void* d, struct wp_drm_lease_device_v1* dev) { (void)d; (void)dev; }
static void spare_released(void* d, struct wp_drm_lease_device_v1* dev) { (void)d; (void)dev; }

static const struct wp_drm_lease_device_v1_listener spare_listener = {
    spare_drm_fd, spare_connector, spare_done, spare_released,
};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, wp_drm_lease_device_v1_interface.name)) {
        lease_dev = wl_registry_bind(r, name, &wp_drm_lease_device_v1_interface, 1);
        wp_drm_lease_device_v1_add_listener(lease_dev, &dev_listener, NULL);
    }
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!lease_dev) {
        fprintf(stderr, "no wp_drm_lease_device_v1\n");
        return 1;
    }

    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (!device_done || device_drm_fd < 0) {
        fprintf(stderr, "no drm fd or done from the lease device\n");
        return 1;
    }

    struct stat st;

    if (fstat(device_drm_fd, &st) != 0) {
        fprintf(stderr, "the device fd is not usable\n");
        return 1;
    }

    if (!offered) {
        fprintf(stderr, "no connector offered for lease\n");
        return 1;
    }

    printf("offered %s id %u\n", offered_name, offered_id);

    /* an empty request is a protocol error on a throwaway device object, so
     * the main one survives for the real lease below */
    struct wl_display* err_dpy = wl_display_connect(NULL);

    if (err_dpy) {
        struct wl_registry* er = wl_display_get_registry(err_dpy);
        struct wp_drm_lease_device_v1* ed = NULL;

        /* bind by the same name the main connection saw: re-enumerate */
        struct wl_registry_listener el = {extra_global, extra_remove};
        struct wp_drm_lease_device_v1* saved = lease_dev;

        lease_dev = NULL;
        wl_registry_add_listener(er, &el, NULL);
        wl_display_roundtrip(err_dpy);
        ed = lease_dev;
        lease_dev = saved;

        if (ed) {
            wp_drm_lease_device_v1_add_listener(ed, &spare_listener, NULL);
            wl_display_roundtrip(err_dpy);

            struct wp_drm_lease_request_v1* empty = wp_drm_lease_device_v1_create_lease_request(ed);
            struct wp_drm_lease_v1* born = wp_drm_lease_request_v1_submit(empty);

            wl_display_roundtrip(err_dpy);

            int err = wl_display_get_error(err_dpy);

            if (!err) {
                fprintf(stderr, "an empty lease request was accepted\n");
                return 1;
            }

            puts("empty request refused");
            wp_drm_lease_v1_destroy(born);
            wp_drm_lease_device_v1_destroy(ed);
        }

        wl_registry_destroy(er);
        wl_display_disconnect(err_dpy);
    }

    /* the real lease */
    struct wp_drm_lease_request_v1* req = wp_drm_lease_device_v1_create_lease_request(lease_dev);

    wp_drm_lease_request_v1_request_connector(req, offered);

    struct wp_drm_lease_v1* lease = wp_drm_lease_request_v1_submit(req);

    wp_drm_lease_v1_add_listener(lease, &lease_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (lease_fd < 0 || lease_finished) {
        fprintf(stderr, "no lease fd (finished=%d)\n", lease_finished);
        return 1;
    }

    if (fstat(lease_fd, &st) != 0) {
        fprintf(stderr, "the lease fd is not usable\n");
        return 1;
    }

    puts("leased");
    close(lease_fd);
    wp_drm_lease_v1_destroy(lease);
    wl_display_roundtrip(wl_dpy);

    /* requesting one connector twice is a protocol error */
    struct wp_drm_lease_request_v1* dup = wp_drm_lease_device_v1_create_lease_request(lease_dev);

    wp_drm_lease_request_v1_request_connector(dup, offered);
    wp_drm_lease_request_v1_request_connector(dup, offered);
    wl_display_roundtrip(wl_dpy);

    if (!wl_display_get_error(wl_dpy)) {
        fprintf(stderr, "a duplicate connector was accepted\n");
        return 1;
    }

    puts("duplicate refused");

    /* the proxies are client-side allocations; a sanitized run wants them
     * all back even though the connection is already dead */
    wp_drm_lease_request_v1_destroy(dup);
    wp_drm_lease_connector_v1_destroy(offered);
    wp_drm_lease_device_v1_destroy(lease_dev);
    wl_registry_destroy(registry);

    return 0;
}
