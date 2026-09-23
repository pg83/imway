#include "wl_util.h"

#include <ext-data-control-v1-client-protocol.h>
#include <primary-selection-unstable-v1-client-protocol.h>

// A selection replaced by the next one cancels the source it displaces, on
// every kind of source and slot: a wl_data_source clipboard, a primary
// selection source, and data-control sources in both the clipboard and the
// primary slot. A clipboard cleared with a null source cancels the source
// it held. A selection under a bogus serial takes no slot, and a drag
// started with no button held is cancelled at once. A clipboard source its
// owner destroys empties the clipboard, which the focused client hears as
// a null selection; clearing an empty clipboard sends nothing.

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

static int selection_events, null_selections;

static void dd_data_offer(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)o;
}
static void dd_enter(void* d, struct wl_data_device* dd, uint32_t s, struct wl_surface* su,
                     wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)s; (void)su; (void)x; (void)y; (void)o;
}
static void dd_leave(void* d, struct wl_data_device* dd) { (void)d; (void)dd; }
static void dd_motion(void* d, struct wl_data_device* dd, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dd; (void)t; (void)x; (void)y;
}
static void dd_drop(void* d, struct wl_data_device* dd) { (void)d; (void)dd; }
static void dd_selection(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd;
    selection_events++;
    null_selections += o == NULL;
}
static const struct wl_data_device_listener dd_listener = {
    dd_data_offer, dd_enter, dd_leave, dd_motion, dd_drop, dd_selection,
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

    wl_data_device_add_listener(dd, &dd_listener, NULL);
    struct zwp_primary_selection_device_v1* pd =
        zwp_primary_selection_device_manager_v1_get_device(primary_mgr, wl_seat_g);
    struct ext_data_control_device_v1* dcd = ext_data_control_manager_v1_get_data_device(dc_mgr, wl_seat_g);

    // a selection under a serial the client never had takes no slot: the
    // valid one after it displaces nothing
    wl_data_device_set_selection(dd, wl_source(), serial + 1000);
    wl_data_device_set_selection(dd, wl_source(), serial);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
    if (cancelled) {
        fprintf(stderr, "a selection under a bogus serial took the slot\n");
        return 1;
    }
    printf("bogus serial: ok\n");

    wl_data_device_set_selection(dd, wl_source(), serial);
    if (expect_one_cancel("clipboard"))
        return 1;

    // clearing the clipboard cancels its source just the same, and a
    // selection set after it displaces nothing
    wl_data_device_set_selection(dd, NULL, serial);
    if (expect_one_cancel("clipboard cleared"))
        return 1;
    wl_data_device_set_selection(dd, wl_source(), serial);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
    if (cancelled) {
        fprintf(stderr, "a selection into the cleared clipboard cancelled a source\n");
        return 1;
    }

    // a drag with no button held is refused, the source cancelled
    wl_data_device_start_drag(dd, wl_source(), ctx.surface, NULL, serial);
    if (expect_one_cancel("drag without a grab"))
        return 1;

    // the primary selection refuses a bogus serial the same way
    zwp_primary_selection_device_v1_set_selection(pd, primary_source(), serial + 1000);
    zwp_primary_selection_device_v1_set_selection(pd, primary_source(), serial);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
    if (cancelled) {
        fprintf(stderr, "a primary selection under a bogus serial took the slot\n");
        return 1;
    }
    printf("primary bogus serial: ok\n");

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

    struct wl_data_source* last = wl_source();

    wl_data_device_set_selection(dd, last, serial);
    if (expect_one_cancel("clipboard over data-control"))
        return 1;

    int nulls = null_selections;

    wl_data_source_destroy(last);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
    if (null_selections != nulls + 1) {
        fprintf(stderr, "the destroyed clipboard source left no null selection (%d -> %d)\n",
                nulls, null_selections);
        return 1;
    }
    printf("destroyed clipboard source: ok\n");

    int events = selection_events;

    wl_data_device_set_selection(dd, NULL, serial);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
    if (selection_events != events) {
        fprintf(stderr, "clearing an empty clipboard sent %d selection events\n",
                selection_events - events);
        return 1;
    }
    printf("clearing an empty clipboard: ok\n");

    printf("selection replace done\n");

    return 0;
}
