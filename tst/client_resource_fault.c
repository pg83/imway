// Walks one request chain up to the resource the scenario's IMWAY_CHAOS
// makes the compositor fail to allocate, and checks how the failure reaches
// the client: argv[2] "fault" expects it, "ok" expects the same chain to
// succeed once the fault is spent. Every global the client knows is bound
// on connect, so the "bind" chain is the bind of whichever global is armed.
//   usage: client_resource_fault MODE fault|ok
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <viewporter-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <primary-selection-unstable-v1-client-protocol.h>
#include <tablet-v2-client-protocol.h>
#include <xdg-dialog-v1-client-protocol.h>
#include <pointer-warp-v1-client-protocol.h>
#include <tearing-control-v1-client-protocol.h>
#include <fifo-v1-client-protocol.h>
#include <commit-timing-v1-client-protocol.h>
#include <ext-foreign-toplevel-list-v1-client-protocol.h>
#include <ext-data-control-v1-client-protocol.h>
#include <text-input-unstable-v3-client-protocol.h>
#include <xdg-toplevel-tag-v1-client-protocol.h>
#include <xdg-foreign-unstable-v2-client-protocol.h>
#include <xdg-toplevel-drag-v1-client-protocol.h>
#include <appmenu-client-protocol.h>
#include <input-method-unstable-v2-client-protocol.h>
#include <virtual-keyboard-unstable-v1-client-protocol.h>

// the globals, each bound once at the version given (0: the interface's own)
static struct {
    const struct wl_interface* iface;
    uint32_t version;
    void* proxy;
} globals[] = {
    {&wl_compositor_interface, 0, NULL},
    {&wl_subcompositor_interface, 1, NULL},
    {&wl_shm_interface, 1, NULL},
    {&wl_seat_interface, 1, NULL},
    {&wl_output_interface, 1, NULL},
    {&wl_data_device_manager_interface, 1, NULL},
    {&xdg_wm_base_interface, 1, NULL},
    {&wp_viewporter_interface, 1, NULL},
    {&zxdg_decoration_manager_v1_interface, 1, NULL},
    {&zwp_primary_selection_device_manager_v1_interface, 1, NULL},
    {&zwp_tablet_manager_v2_interface, 1, NULL},
    {&xdg_wm_dialog_v1_interface, 1, NULL},
    {&wp_pointer_warp_v1_interface, 1, NULL},
    {&wp_tearing_control_manager_v1_interface, 1, NULL},
    {&wp_fifo_manager_v1_interface, 1, NULL},
    {&wp_commit_timing_manager_v1_interface, 1, NULL},
    {&ext_foreign_toplevel_list_v1_interface, 1, NULL},
    {&ext_data_control_manager_v1_interface, 1, NULL},
    {&zwp_text_input_manager_v3_interface, 1, NULL},
    {&xdg_toplevel_tag_manager_v1_interface, 1, NULL},
    {&zxdg_exporter_v2_interface, 1, NULL},
    {&zxdg_importer_v2_interface, 1, NULL},
    {&xdg_toplevel_drag_manager_v1_interface, 1, NULL},
    {&org_kde_kwin_appmenu_manager_interface, 1, NULL},
    {&zwp_input_method_manager_v2_interface, 1, NULL},
    {&zwp_virtual_keyboard_manager_v1_interface, 1, NULL},
};

#define NGLOBALS (sizeof(globals) / sizeof(globals[0]))

static void* global(const struct wl_interface* iface) {
    for (size_t i = 0; i < NGLOBALS; i++) {
        if (globals[i].iface == iface) {
            if (!globals[i].proxy) {
                fprintf(stderr, "no %s global\n", iface->name);
                exit(2);
            }

            return globals[i].proxy;
        }
    }

    exit(2);
}

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data;

    for (size_t i = 0; i < NGLOBALS; i++) {
        if (globals[i].proxy || strcmp(interface, globals[i].iface->name)) {
            continue;
        }

        uint32_t v = globals[i].version ? globals[i].version : (uint32_t)globals[i].iface->version;

        globals[i].proxy = wl_registry_bind(registry, name, globals[i].iface, v < version ? v : version);
    }
}

static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static struct wl_display* display;

#define G(name) ((struct name*)global(&name##_interface))

// ---- a mapped, keyboard-focused toplevel ----------------------------------

static int configured;
static int focused;

static void wm_ping(void* d, struct xdg_wm_base* wm, uint32_t serial) {
    (void)d;
    xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_listener = {.ping = wm_ping};

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    configured = 1;
}

static const struct xdg_surface_listener xs_listener = {.configure = xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* s) {
    (void)d; (void)t; (void)w; (void)h; (void)s;
}

static void tl_close(void* d, struct xdg_toplevel* t) {
    (void)d; (void)t;
}

static const struct xdg_toplevel_listener tl_listener = {.configure = tl_configure, .close = tl_close};

static void kb_keymap(void* d, struct wl_keyboard* k, uint32_t f, int32_t fd, uint32_t s) {
    (void)d; (void)k; (void)f; (void)s;
    close(fd);
}

static void kb_enter(void* d, struct wl_keyboard* k, uint32_t serial, struct wl_surface* s, struct wl_array* keys) {
    (void)d; (void)k; (void)serial; (void)s; (void)keys;
    focused = 1;
}

static void kb_leave(void* d, struct wl_keyboard* k, uint32_t serial, struct wl_surface* s) {
    (void)d; (void)k; (void)serial; (void)s;
    focused = 0;
}

static void kb_key(void* d, struct wl_keyboard* k, uint32_t serial, uint32_t t, uint32_t key, uint32_t st) {
    (void)d; (void)k; (void)serial; (void)t; (void)key; (void)st;
}

static void kb_modifiers(void* d, struct wl_keyboard* k, uint32_t serial, uint32_t a, uint32_t b, uint32_t c, uint32_t g) {
    (void)d; (void)k; (void)serial; (void)a; (void)b; (void)c; (void)g;
}

static const struct wl_keyboard_listener kb_listener = {
    .keymap = kb_keymap,
    .enter = kb_enter,
    .leave = kb_leave,
    .key = kb_key,
    .modifiers = kb_modifiers,
};

static struct xdg_surface* last_xs;

static struct xdg_toplevel* toplevel_of(struct wl_surface* surface) {
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(G(xdg_wm_base), surface);

    last_xs = xs;

    xdg_surface_add_listener(xs, &xs_listener, NULL);

    struct xdg_toplevel* t = xdg_surface_get_toplevel(xs);

    xdg_toplevel_add_listener(t, &tl_listener, NULL);

    return t;
}

static struct wl_buffer* shm_buffer(int w, int h) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("resource-fault", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        exit(2);
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(G(wl_shm), fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buf;
}

// maps a toplevel and waits for it to take the keyboard; on a fault that
// fires on the way (the foreign-toplevel handle announced at map), returns
// 0 with the display already dead
static int map_focused_toplevel(void) {
    struct wl_surface* surface = wl_compositor_create_surface(G(wl_compositor));
    struct wl_keyboard* kb = wl_seat_get_keyboard(G(wl_seat));

    wl_keyboard_add_listener(kb, &kb_listener, NULL);
    toplevel_of(surface);
    wl_surface_commit(surface);

    for (int i = 0; i < 500 && !configured; i++) {
        if (wl_display_roundtrip(display) < 0) {
            return 0;
        }
    }

    wl_surface_attach(surface, shm_buffer(64, 64), 0, 0);
    wl_surface_damage(surface, 0, 0, 64, 64);
    wl_surface_commit(surface);

    for (int i = 0; i < 500 && !focused; i++) {
        if (wl_display_roundtrip(display) < 0) {
            return 0;
        }

        usleep(10000);
    }

    if (!focused) {
        fprintf(stderr, "the toplevel never took the keyboard\n");
        exit(1);
    }

    return 1;
}

// ---- selection offers -----------------------------------------------------
// A selection offer the compositor cannot allocate is not a client error:
// the device is told there is no selection. seen counts selection events
// after the selection was set, offer is the last one's.

static int seen;
static void* offer;

static void dd_data_offer(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)o;
}

static void dd_enter(void* d, struct wl_data_device* dd, uint32_t s, struct wl_surface* sf, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)s; (void)sf; (void)x; (void)y; (void)o;
}

static void dd_leave(void* d, struct wl_data_device* dd) {
    (void)d; (void)dd;
}

static void dd_motion(void* d, struct wl_data_device* dd, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dd; (void)t; (void)x; (void)y;
}

static void dd_drop(void* d, struct wl_data_device* dd) {
    (void)d; (void)dd;
}

static void dd_selection(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd;
    seen++;
    offer = o;
}

static const struct wl_data_device_listener dd_listener = {
    .data_offer = dd_data_offer,
    .enter = dd_enter,
    .leave = dd_leave,
    .motion = dd_motion,
    .drop = dd_drop,
    .selection = dd_selection,
};

static void pd_data_offer(void* d, struct zwp_primary_selection_device_v1* pd, struct zwp_primary_selection_offer_v1* o) {
    (void)d; (void)pd; (void)o;
}

static void pd_selection(void* d, struct zwp_primary_selection_device_v1* pd, struct zwp_primary_selection_offer_v1* o) {
    (void)d; (void)pd;
    seen++;
    offer = o;
}

static const struct zwp_primary_selection_device_v1_listener pd_listener = {
    .data_offer = pd_data_offer,
    .selection = pd_selection,
};

static void dc_data_offer(void* d, struct ext_data_control_device_v1* dc, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dc; (void)o;
}

static void dc_selection(void* d, struct ext_data_control_device_v1* dc, struct ext_data_control_offer_v1* o) {
    (void)dc;
    if (d) {
        seen++;
        offer = o;
    }
}

static void dc_finished(void* d, struct ext_data_control_device_v1* dc) {
    (void)d; (void)dc;
}

static void dc_primary(void* d, struct ext_data_control_device_v1* dc, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dc; (void)o;
}

static const struct ext_data_control_device_v1_listener dc_listener = {
    .data_offer = dc_data_offer,
    .selection = dc_selection,
    .finished = dc_finished,
    .primary_selection = dc_primary,
};

// sets the clipboard (or the primary selection) through data control, which
// needs no focus, so the focused client's own devices are sent the offer;
// watch_dc: count the data-control device's clipboard event instead
static void set_selection_via_dc(int primary, int watch_dc) {
    struct ext_data_control_device_v1* dev =
        ext_data_control_manager_v1_get_data_device(G(ext_data_control_manager_v1), G(wl_seat));

    ext_data_control_device_v1_add_listener(dev, &dc_listener, watch_dc ? &seen : NULL);

    if (wl_display_roundtrip(display) < 0) {
        exit(1);
    }

    seen = 0;
    offer = NULL;

    struct ext_data_control_source_v1* src =
        ext_data_control_manager_v1_create_data_source(G(ext_data_control_manager_v1));

    ext_data_control_source_v1_offer(src, "text/plain");

    if (primary) {
        ext_data_control_device_v1_set_primary_selection(dev, src);
    } else {
        ext_data_control_device_v1_set_selection(dev, src);
    }
}

// ---- the chains -----------------------------------------------------------

static struct xdg_positioner* sized_positioner(void) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(G(xdg_wm_base));

    xdg_positioner_set_size(pos, 10, 10);
    xdg_positioner_set_anchor_rect(pos, 0, 0, 1, 1);

    return pos;
}

static struct wl_surface* surface(void) {
    return wl_compositor_create_surface(G(wl_compositor));
}

// walks the chain; returns 1 when the mode ends in a selection offer the
// caller must check instead of the connection's fate
static int chain(const char* mode) {
    if (!strcmp(mode, "bind")) {
        return 0;
    }

    if (!strcmp(mode, "shm-pool")) {
        int fd = memfd_create("resource-fault", 0);

        if (fd < 0 || ftruncate(fd, 4096) < 0) {
            exit(2);
        }

        wl_shm_create_pool(G(wl_shm), fd, 4096);
        close(fd);

        return 0;
    }

    if (!strcmp(mode, "shm-buffer")) {
        shm_buffer(16, 16);

        return 0;
    }

    if (!strcmp(mode, "surface")) {
        surface();

        return 0;
    }

    if (!strcmp(mode, "region")) {
        wl_compositor_create_region(G(wl_compositor));

        return 0;
    }

    if (!strcmp(mode, "frame")) {
        wl_surface_frame(surface());

        return 0;
    }

    if (!strcmp(mode, "release")) {
        wl_surface_get_release(surface());

        return 0;
    }

    if (!strcmp(mode, "subsurface")) {
        wl_subcompositor_get_subsurface(G(wl_subcompositor), surface(), surface());

        return 0;
    }

    if (!strcmp(mode, "positioner")) {
        xdg_wm_base_create_positioner(G(xdg_wm_base));

        return 0;
    }

    if (!strcmp(mode, "xdg-surface")) {
        xdg_wm_base_get_xdg_surface(G(xdg_wm_base), surface());

        return 0;
    }

    if (!strcmp(mode, "toplevel")) {
        toplevel_of(surface());

        return 0;
    }

    if (!strcmp(mode, "popup")) {
        toplevel_of(surface());

        struct xdg_surface* parent = last_xs;
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(G(xdg_wm_base), surface());

        xdg_surface_get_popup(xs, parent, sized_positioner());

        return 0;
    }

    if (!strcmp(mode, "data-source")) {
        wl_data_device_manager_create_data_source(G(wl_data_device_manager));

        return 0;
    }

    if (!strcmp(mode, "data-device")) {
        wl_data_device_manager_get_data_device(G(wl_data_device_manager), G(wl_seat));

        return 0;
    }

    if (!strcmp(mode, "data-offer")) {
        map_focused_toplevel();

        struct wl_data_device* dd = wl_data_device_manager_get_data_device(G(wl_data_device_manager), G(wl_seat));

        wl_data_device_add_listener(dd, &dd_listener, NULL);
        set_selection_via_dc(0, 0);

        return 1;
    }

    if (!strcmp(mode, "primary-source")) {
        zwp_primary_selection_device_manager_v1_create_source(G(zwp_primary_selection_device_manager_v1));

        return 0;
    }

    if (!strcmp(mode, "primary-device")) {
        zwp_primary_selection_device_manager_v1_get_device(G(zwp_primary_selection_device_manager_v1), G(wl_seat));

        return 0;
    }

    if (!strcmp(mode, "primary-offer")) {
        map_focused_toplevel();

        struct zwp_primary_selection_device_v1* pd =
            zwp_primary_selection_device_manager_v1_get_device(G(zwp_primary_selection_device_manager_v1), G(wl_seat));

        zwp_primary_selection_device_v1_add_listener(pd, &pd_listener, NULL);
        set_selection_via_dc(1, 0);

        return 1;
    }

    if (!strcmp(mode, "dc-source")) {
        ext_data_control_manager_v1_create_data_source(G(ext_data_control_manager_v1));

        return 0;
    }

    if (!strcmp(mode, "dc-device")) {
        ext_data_control_manager_v1_get_data_device(G(ext_data_control_manager_v1), G(wl_seat));

        return 0;
    }

    if (!strcmp(mode, "dc-offer")) {
        set_selection_via_dc(0, 1);

        return 1;
    }

    if (!strcmp(mode, "toplevel-drag")) {
        struct wl_data_source* src = wl_data_device_manager_create_data_source(G(wl_data_device_manager));

        xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag(G(xdg_toplevel_drag_manager_v1), src);

        return 0;
    }

    if (!strcmp(mode, "decoration")) {
        zxdg_decoration_manager_v1_get_toplevel_decoration(G(zxdg_decoration_manager_v1), toplevel_of(surface()));

        return 0;
    }

    if (!strcmp(mode, "dialog")) {
        xdg_wm_dialog_v1_get_xdg_dialog(G(xdg_wm_dialog_v1), toplevel_of(surface()));

        return 0;
    }

    if (!strcmp(mode, "appmenu")) {
        org_kde_kwin_appmenu_manager_create(G(org_kde_kwin_appmenu_manager), surface());

        return 0;
    }

    if (!strcmp(mode, "viewport")) {
        wp_viewporter_get_viewport(G(wp_viewporter), surface());

        return 0;
    }

    if (!strcmp(mode, "tearing")) {
        wp_tearing_control_manager_v1_get_tearing_control(G(wp_tearing_control_manager_v1), surface());

        return 0;
    }

    if (!strcmp(mode, "fifo")) {
        wp_fifo_manager_v1_get_fifo(G(wp_fifo_manager_v1), surface());

        return 0;
    }

    if (!strcmp(mode, "commit-timer")) {
        wp_commit_timing_manager_v1_get_timer(G(wp_commit_timing_manager_v1), surface());

        return 0;
    }

    if (!strcmp(mode, "export")) {
        struct wl_surface* s = surface();

        toplevel_of(s);
        zxdg_exporter_v2_export_toplevel(G(zxdg_exporter_v2), s);

        return 0;
    }

    if (!strcmp(mode, "import")) {
        zxdg_importer_v2_import_toplevel(G(zxdg_importer_v2), "no-such-handle");

        return 0;
    }

    if (!strcmp(mode, "foreign-handle")) {
        map_focused_toplevel();

        return 0;
    }

    if (!strcmp(mode, "text-input")) {
        zwp_text_input_manager_v3_get_text_input(G(zwp_text_input_manager_v3), G(wl_seat));

        return 0;
    }

    if (!strcmp(mode, "input-method")) {
        zwp_input_method_manager_v2_get_input_method(G(zwp_input_method_manager_v2), G(wl_seat));

        return 0;
    }

    if (!strcmp(mode, "im-popup")) {
        struct zwp_input_method_v2* im =
            zwp_input_method_manager_v2_get_input_method(G(zwp_input_method_manager_v2), G(wl_seat));

        zwp_input_method_v2_get_input_popup_surface(im, surface());

        return 0;
    }

    if (!strcmp(mode, "im-grab")) {
        struct zwp_input_method_v2* im =
            zwp_input_method_manager_v2_get_input_method(G(zwp_input_method_manager_v2), G(wl_seat));

        zwp_input_method_v2_grab_keyboard(im);

        return 0;
    }

    if (!strcmp(mode, "virtual-keyboard")) {
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(G(zwp_virtual_keyboard_manager_v1), G(wl_seat));

        return 0;
    }

    if (!strcmp(mode, "tablet-seat")) {
        zwp_tablet_manager_v2_get_tablet_seat(G(zwp_tablet_manager_v2), G(wl_seat));

        return 0;
    }

    fprintf(stderr, "unknown mode %s\n", mode);
    exit(2);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }

    int expectFault = !strcmp(argv[2], "fault");

    display = wl_display_connect(NULL);

    if (!display) {
        return 2;
    }

    struct wl_registry* registry = wl_display_get_registry(display);

    wl_registry_add_listener(registry, &registry_listener, NULL);

    // the registry arrives with the first roundtrip, the binds it triggers
    // are answered by the second
    if (wl_display_roundtrip(display) < 0) {
        return 2;
    }

    int bound = wl_display_roundtrip(display) >= 0;

    if (bound) {
        for (size_t i = 0; i < NGLOBALS; i++) {
            if (globals[i].iface == &xdg_wm_base_interface && globals[i].proxy) {
                xdg_wm_base_add_listener(globals[i].proxy, &wm_listener, NULL);
            }
        }
    }

    int offerMode = bound ? chain(argv[1]) : 0;
    int alive = bound && wl_display_roundtrip(display) >= 0 && wl_display_roundtrip(display) >= 0;

    if (offerMode && alive) {
        if (seen != 1) {
            fprintf(stderr, "%s: %d selection events, expected one\n", argv[1], seen);
            return 1;
        }

        if (expectFault ? offer != NULL : offer == NULL) {
            fprintf(stderr, "%s: selection offer %s\n", argv[1], offer ? "made" : "missing");
            return 1;
        }

        printf("%s: selection %s\n", argv[1], offer ? "offered" : "dropped");

        return 0;
    }

    if (!expectFault) {
        if (!alive) {
            fprintf(stderr, "%s: chain failed: errno=%d\n", argv[1], wl_display_get_error(display));
            return 1;
        }

        printf("%s: ok\n", argv[1]);

        return 0;
    }

    const struct wl_interface* iface = NULL;
    uint32_t id = 0;
    uint32_t code = wl_display_get_protocol_error(display, &iface, &id);

    // libwayland reports a display error as its errno, not EPROTO
    if (alive || wl_display_get_error(display) != ENOMEM || !iface || iface != &wl_display_interface ||
        code != WL_DISPLAY_ERROR_NO_MEMORY) {
        fprintf(stderr, "%s: expected no_memory, got alive=%d errno=%d iface=%s code=%u\n", argv[1], alive,
                wl_display_get_error(display), iface ? iface->name : "(none)", code);
        return 1;
    }

    printf("%s: no_memory\n", argv[1]);

    return 0;
}
