#include "wl_util.h"

#include <ext-data-control-v1-client-protocol.h>
#include <primary-selection-unstable-v1-client-protocol.h>

// A privileged client sets the primary selection through ext-data-control;
// the focused client's zwp_primary_selection_device must get a primary
// selection offer it can receive the payload through, like the one a
// zwp_primary_selection_source would make. Devices made after the selection
// exists, a primary one and a data-control one, start out holding it.

static struct ext_data_control_manager_v1* mgr;
static struct zwp_primary_selection_device_manager_v1* primary_mgr;
static struct wl_seat* seat2;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, ext_data_control_manager_v1_interface.name))
        mgr = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_primary_selection_device_manager_v1_interface.name))
        primary_mgr = wl_registry_bind(registry, name,
                                       &zwp_primary_selection_device_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat2)
        seat2 = wl_registry_bind(registry, name, &wl_seat_interface, 5);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static const char kPayload[] = "dc-primary";

// ---- dc source ----
static void dc_send(void* d, struct ext_data_control_source_v1* s,
                    const char* mime, int32_t fd) {
    (void)d; (void)s; (void)mime;
    if (write(fd, kPayload, sizeof(kPayload) - 1) < 0)
        perror("write");
    close(fd);
}
static void dc_cancelled(void* d, struct ext_data_control_source_v1* s) {
    (void)d; (void)s;
}
static const struct ext_data_control_source_v1_listener dc_source_listener = {
    .send = dc_send,
    .cancelled = dc_cancelled,
};

// ---- the focused client's primary device ----
static struct zwp_primary_selection_offer_v1* primary_offer;
static int primary_has_mime, primary_selection_seen;

static void primary_offer_mime(void* d, struct zwp_primary_selection_offer_v1* o, const char* mime) {
    (void)d; (void)o;
    if (!strcmp(mime, "text/plain"))
        primary_has_mime = 1;
}
static const struct zwp_primary_selection_offer_v1_listener primary_offer_listener = {
    .offer = primary_offer_mime,
};

static void primary_data_offer(void* d, struct zwp_primary_selection_device_v1* dev,
                               struct zwp_primary_selection_offer_v1* o) {
    (void)d; (void)dev;
    zwp_primary_selection_offer_v1_add_listener(o, &primary_offer_listener, NULL);
}
static void primary_selection(void* d, struct zwp_primary_selection_device_v1* dev,
                              struct zwp_primary_selection_offer_v1* o) {
    (void)d; (void)dev;
    if (o) {
        primary_offer = o;
        primary_selection_seen = 1;
    }
}
static const struct zwp_primary_selection_device_v1_listener primary_device_listener = {
    .data_offer = primary_data_offer,
    .selection = primary_selection,
};

// ---- devices made once the selection exists ----
static int late_primary_seen;

static void late_primary_selection(void* d, struct zwp_primary_selection_device_v1* dev,
                                   struct zwp_primary_selection_offer_v1* o) {
    (void)d; (void)dev;
    if (o)
        late_primary_seen = 1;
}
static const struct zwp_primary_selection_device_v1_listener late_primary_listener = {
    .data_offer = primary_data_offer,
    .selection = late_primary_selection,
};

static int late_dc_primary_seen;

static void dc_offer_mime(void* d, struct ext_data_control_offer_v1* o, const char* mime) {
    (void)d; (void)o; (void)mime;
}
static const struct ext_data_control_offer_v1_listener dc_offer_listener = {
    .offer = dc_offer_mime,
};
static void dc_data_offer(void* d, struct ext_data_control_device_v1* dev, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dev;
    ext_data_control_offer_v1_add_listener(o, &dc_offer_listener, NULL);
}
static void dc_selection(void* d, struct ext_data_control_device_v1* dev, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dev; (void)o;
}
static void dc_finished(void* d, struct ext_data_control_device_v1* dev) {
    (void)d; (void)dev;
}
static void dc_primary_selection(void* d, struct ext_data_control_device_v1* dev, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dev;
    if (o)
        late_dc_primary_seen = 1;
}
static const struct ext_data_control_device_v1_listener late_dc_listener = {
    .data_offer = dc_data_offer,
    .selection = dc_selection,
    .finished = dc_finished,
    .primary_selection = dc_primary_selection,
};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!mgr || !primary_mgr || !seat2) {
        fprintf(stderr, "missing globals (dc=%p primary=%p seat=%p)\n",
                (void*)mgr, (void*)primary_mgr, (void*)seat2);
        return 2;
    }

    // a focused toplevel so the primary device has a selection target
    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "dc-primary-target", 200, 200, 0xff008000);

    struct zwp_primary_selection_device_v1* primary_dev =
        zwp_primary_selection_device_manager_v1_get_device(primary_mgr, seat2);
    zwp_primary_selection_device_v1_add_listener(primary_dev, &primary_device_listener, NULL);

    struct ext_data_control_device_v1* dc_dev =
        ext_data_control_manager_v1_get_data_device(mgr, seat2);
    wl_display_roundtrip(wl_dpy);

    struct ext_data_control_source_v1* src =
        ext_data_control_manager_v1_create_data_source(mgr);
    ext_data_control_source_v1_add_listener(src, &dc_source_listener, NULL);
    ext_data_control_source_v1_offer(src, "text/plain");
    ext_data_control_device_v1_set_primary_selection(dc_dev, src);

    while (!primary_selection_seen && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!primary_selection_seen || !primary_has_mime) {
        fprintf(stderr, "no primary offer with text/plain (seen=%d)\n", primary_selection_seen);
        return 1;
    }

    int fds[2];
    if (pipe(fds) < 0) return 2;
    zwp_primary_selection_offer_v1_receive(primary_offer, "text/plain", fds[1]);
    close(fds[1]);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "receive on the primary offer killed the connection\n");
        return 1;
    }

    char buf[64] = {0};
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    if (n < 0 || strcmp(buf, kPayload)) {
        fprintf(stderr, "payload mismatch: got \"%s\"\n", buf);
        return 1;
    }

    struct zwp_primary_selection_device_v1* late_primary =
        zwp_primary_selection_device_manager_v1_get_device(primary_mgr, seat2);
    struct ext_data_control_device_v1* late_dc = ext_data_control_manager_v1_get_data_device(mgr, seat2);

    zwp_primary_selection_device_v1_add_listener(late_primary, &late_primary_listener, NULL);
    ext_data_control_device_v1_add_listener(late_dc, &late_dc_listener, NULL);
    if (wl_display_roundtrip(wl_dpy) < 0 || !late_primary_seen || !late_dc_primary_seen) {
        fprintf(stderr, "devices made after the selection missed it: primary=%d data-control=%d\n",
                late_primary_seen, late_dc_primary_seen);
        return 1;
    }

    printf("data-control primary done\n");
    fflush(stdout);
    return 0;
}
