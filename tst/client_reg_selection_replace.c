#include "wl_util.h"

#include <ext-data-control-v1-client-protocol.h>
#include <primary-selection-unstable-v1-client-protocol.h>

// A selection replaced by the next one cancels the source it displaces, on
// every kind of source and slot: a wl_data_source clipboard, a primary
// selection source, and data-control sources in both the clipboard and the
// primary slot.

static struct zwp_primary_selection_device_manager_v1* primary_mgr;
static struct ext_data_control_manager_v1* dc_mgr;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, ext_data_control_manager_v1_interface.name))
        dc_mgr = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_primary_selection_device_manager_v1_interface.name))
        primary_mgr = wl_registry_bind(registry, name, &zwp_primary_selection_device_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int cancelled;

static void wl_src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void wl_src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    close(fd);
}
static void wl_src_cancelled(void* d, struct wl_data_source* s) {
    (void)d; (void)s;
    cancelled++;
}
static const struct wl_data_source_listener wl_src_listener = {
    .target = wl_src_target,
    .send = wl_src_send,
    .cancelled = wl_src_cancelled,
};

static void ps_send(void* d, struct zwp_primary_selection_source_v1* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    close(fd);
}
static void ps_cancelled(void* d, struct zwp_primary_selection_source_v1* s) {
    (void)d; (void)s;
    cancelled++;
}
static const struct zwp_primary_selection_source_v1_listener ps_listener = {
    .send = ps_send,
    .cancelled = ps_cancelled,
};

static void dc_send(void* d, struct ext_data_control_source_v1* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    close(fd);
}
static void dc_cancelled(void* d, struct ext_data_control_source_v1* s) {
    (void)d; (void)s;
    cancelled++;
}
static const struct ext_data_control_source_v1_listener dc_listener = {
    .send = dc_send,
    .cancelled = dc_cancelled,
};

static struct wl_data_source* wl_source(void) {
    struct wl_data_source* s = wl_data_device_manager_create_data_source(wl_ddm);

    wl_data_source_add_listener(s, &wl_src_listener, NULL);
    wl_data_source_offer(s, "text/plain");

    return s;
}

static struct zwp_primary_selection_source_v1* primary_source(void) {
    struct zwp_primary_selection_source_v1* s =
        zwp_primary_selection_device_manager_v1_create_source(primary_mgr);

    zwp_primary_selection_source_v1_add_listener(s, &ps_listener, NULL);
    zwp_primary_selection_source_v1_offer(s, "text/plain");

    return s;
}

static struct ext_data_control_source_v1* dc_source(void) {
    struct ext_data_control_source_v1* s = ext_data_control_manager_v1_create_data_source(dc_mgr);

    ext_data_control_source_v1_add_listener(s, &dc_listener, NULL);
    ext_data_control_source_v1_offer(s, "text/plain");

    return s;
}

// the first of two sources set in a row must be cancelled, the second not
static int expect_one_cancel(const char* what) {
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (cancelled != 1) {
        fprintf(stderr, "%s: %d sources cancelled, want 1\n", what, cancelled);
        return 1;
    }

    printf("%s: ok\n", what);
    cancelled = 0;

    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!primary_mgr || !dc_mgr || !wl_ddm || !wl_seat_g) return 2;

    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "selection-replace", 200, 200, 0xff305070u);

    while (!wlk_enters && wl_display_dispatch(wl_dpy) != -1) {
    }

    uint32_t serial = wlk_enter_serial;
    struct wl_data_device* dd = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct zwp_primary_selection_device_v1* pd =
        zwp_primary_selection_device_manager_v1_get_device(primary_mgr, wl_seat_g);
    struct ext_data_control_device_v1* dcd = ext_data_control_manager_v1_get_data_device(dc_mgr, wl_seat_g);

    wl_data_device_set_selection(dd, wl_source(), serial);
    wl_data_device_set_selection(dd, wl_source(), serial);
    if (expect_one_cancel("clipboard"))
        return 1;

    zwp_primary_selection_device_v1_set_selection(pd, primary_source(), serial);
    zwp_primary_selection_device_v1_set_selection(pd, primary_source(), serial);
    if (expect_one_cancel("primary"))
        return 1;

    // data-control sources take over both slots, and give them up in turn
    ext_data_control_device_v1_set_selection(dcd, dc_source());
    if (expect_one_cancel("data-control clipboard over wl_data_source"))
        return 1;
    ext_data_control_device_v1_set_selection(dcd, dc_source());
    if (expect_one_cancel("data-control clipboard"))
        return 1;

    ext_data_control_device_v1_set_primary_selection(dcd, dc_source());
    if (expect_one_cancel("data-control primary over primary source"))
        return 1;
    ext_data_control_device_v1_set_primary_selection(dcd, dc_source());
    if (expect_one_cancel("data-control primary"))
        return 1;

    printf("selection replace done\n");

    return 0;
}
