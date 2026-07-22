#include "wl_util.h"

#include <ext-data-control-v1-client-protocol.h>

// #E-11: ext-data-control. A privileged client sets the clipboard without
// any input serial; the focused client's regular wl_data_device receives it,
// and the data-control device itself gets the loopback offer.

static struct ext_data_control_manager_v1* mgr;
static struct wl_seat* seat2;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, ext_data_control_manager_v1_interface.name))
        mgr = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat2)
        seat2 = wl_registry_bind(registry, name, &wl_seat_interface, 5);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static const char kPayload[] = "dc-clip";

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

// ---- dc device: loopback offer ----
static struct ext_data_control_offer_v1* dc_offer_obj;
static int dc_offer_has_mime, dc_selection_seen;

static void dc_offer_mime(void* d, struct ext_data_control_offer_v1* o, const char* mime) {
    (void)d; (void)o;
    if (!strcmp(mime, "text/plain"))
        dc_offer_has_mime = 1;
}
static const struct ext_data_control_offer_v1_listener dc_offer_listener = {
    .offer = dc_offer_mime,
};

static void dc_data_offer(void* d, struct ext_data_control_device_v1* dev,
                          struct ext_data_control_offer_v1* offer) {
    (void)d; (void)dev;
    dc_offer_obj = offer;
    ext_data_control_offer_v1_add_listener(offer, &dc_offer_listener, NULL);
}
static void dc_selection(void* d, struct ext_data_control_device_v1* dev,
                         struct ext_data_control_offer_v1* offer) {
    (void)d; (void)dev;
    if (offer)
        dc_selection_seen = 1;
}
static void dc_finished(void* d, struct ext_data_control_device_v1* dev) {
    (void)d; (void)dev;
}
static void dc_primary(void* d, struct ext_data_control_device_v1* dev,
                       struct ext_data_control_offer_v1* offer) {
    (void)d; (void)dev; (void)offer;
}
static const struct ext_data_control_device_v1_listener dc_device_listener = {
    .data_offer = dc_data_offer,
    .selection = dc_selection,
    .finished = dc_finished,
    .primary_selection = dc_primary,
};

// ---- regular data device ----
static struct wl_data_offer* wl_offer_obj;
static int wl_selection_seen;

static void wl_offer_mime(void* d, struct wl_data_offer* o, const char* mime) {
    (void)d; (void)o; (void)mime;
}
static void wl_offer_source_actions(void* d, struct wl_data_offer* o, uint32_t a) {
    (void)d; (void)o; (void)a;
}
static void wl_offer_action(void* d, struct wl_data_offer* o, uint32_t a) {
    (void)d; (void)o; (void)a;
}
static const struct wl_data_offer_listener wl_offer_listener = {
    .offer = wl_offer_mime,
    .source_actions = wl_offer_source_actions,
    .action = wl_offer_action,
};

static void wl_dev_data_offer(void* d, struct wl_data_device* dev, struct wl_data_offer* o) {
    (void)d; (void)dev;
    wl_offer_obj = o;
    wl_data_offer_add_listener(o, &wl_offer_listener, NULL);
}
static void wl_dev_enter(void* d, struct wl_data_device* dev, uint32_t serial,
                         struct wl_surface* s, wl_fixed_t x, wl_fixed_t y,
                         struct wl_data_offer* o) {
    (void)d; (void)dev; (void)serial; (void)s; (void)x; (void)y; (void)o;
}
static void wl_dev_leave(void* d, struct wl_data_device* dev) { (void)d; (void)dev; }
static void wl_dev_motion(void* d, struct wl_data_device* dev, uint32_t t,
                          wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dev; (void)t; (void)x; (void)y;
}
static void wl_dev_drop(void* d, struct wl_data_device* dev) { (void)d; (void)dev; }
static void wl_dev_selection(void* d, struct wl_data_device* dev, struct wl_data_offer* o) {
    (void)d; (void)dev;
    if (o)
        wl_selection_seen = 1;
}
static const struct wl_data_device_listener wl_device_listener = {
    .data_offer = wl_dev_data_offer,
    .enter = wl_dev_enter,
    .leave = wl_dev_leave,
    .motion = wl_dev_motion,
    .drop = wl_dev_drop,
    .selection = wl_dev_selection,
};

static struct wl_data_device_manager* wl_ddm;

static void ddm_global(void* d, struct wl_registry* registry, uint32_t name,
                       const char* iface, uint32_t version) {
    (void)d;
    if (!strcmp(iface, wl_data_device_manager_interface.name))
        wl_ddm = wl_registry_bind(registry, name, &wl_data_device_manager_interface,
                                  version < 3 ? version : 3);
}
static const struct wl_registry_listener ddm_listener = {ddm_global, extra_remove};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    struct wl_registry* registry2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry2, &ddm_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!mgr || !seat2 || !wl_ddm) {
        fprintf(stderr, "missing globals (dc=%p seat=%p ddm=%p)\n",
                (void*)mgr, (void*)seat2, (void*)wl_ddm);
        return 2;
    }

    // a focused toplevel so the regular device has a selection target
    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "dc-target", 200, 200, 0xff008000);

    struct wl_data_device* wl_dev =
        wl_data_device_manager_get_data_device(wl_ddm, seat2);
    wl_data_device_add_listener(wl_dev, &wl_device_listener, NULL);

    struct ext_data_control_device_v1* dc_dev =
        ext_data_control_manager_v1_get_data_device(mgr, seat2);
    ext_data_control_device_v1_add_listener(dc_dev, &dc_device_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    struct ext_data_control_source_v1* src =
        ext_data_control_manager_v1_create_data_source(mgr);
    ext_data_control_source_v1_add_listener(src, &dc_source_listener, NULL);
    ext_data_control_source_v1_offer(src, "text/plain");
    ext_data_control_device_v1_set_selection(dc_dev, src);
    wl_display_roundtrip(wl_dpy);

    // the focused client's regular device and the dc device both see it
    while ((!wl_selection_seen || !dc_selection_seen) && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!dc_offer_has_mime) {
        fprintf(stderr, "dc loopback offer lacks text/plain\n");
        return 1;
    }

    // receive through the regular offer and compare the payload
    int fds[2];
    if (pipe(fds) < 0) return 2;
    wl_data_offer_receive(wl_offer_obj, "text/plain", fds[1]);
    close(fds[1]);
    wl_display_roundtrip(wl_dpy);

    char buf[64] = {0};
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    if (n < 0 || strcmp(buf, kPayload)) {
        fprintf(stderr, "payload mismatch: got \"%s\"\n", buf);
        return 1;
    }

    printf("data-control done\n");
    fflush(stdout);
    return 0;
}
