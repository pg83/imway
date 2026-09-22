// Rarely used requests of the core, xdg-shell and satellite protocols, one
// mode per scenario. Modes that the scenario checks from the compositor's
// side print "step N" and wait for KEY_1 before the next step; the rest
// check the answer themselves and exit 0 on success.
//   usage: client_wl_misc MODE

#define REG_XDG_VERSION 6
#define REG_COMPOSITOR_VERSION 7
#include "wl_util.h"

#include <linux/input-event-codes.h>

#include <viewporter-client-protocol.h>
#include <tearing-control-v1-client-protocol.h>
#include <xdg-dialog-v1-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <ext-foreign-toplevel-list-v1-client-protocol.h>
#include <ext-data-control-v1-client-protocol.h>
#include <tablet-v2-client-protocol.h>
#include <xdg-toplevel-drag-v1-client-protocol.h>
#include <input-method-unstable-v2-client-protocol.h>
#include <text-input-unstable-v3-client-protocol.h>
#include <xdg-foreign-unstable-v2-client-protocol.h>
#include <xdg-toplevel-icon-v1-client-protocol.h>

static struct wp_viewporter* viewporter;
static struct wp_tearing_control_manager_v1* tearing;
static struct xdg_wm_dialog_v1* dialogs;
static struct zxdg_decoration_manager_v1* decorations;
static struct ext_data_control_manager_v1* dc;
static struct zwp_tablet_manager_v2* tablets;
static struct xdg_toplevel_drag_manager_v1* drags;
static struct zwp_input_method_manager_v2* ims;
static uint32_t foreign_list_name;
static struct zwp_text_input_manager_v3* text_inputs;
static struct zxdg_exporter_v2* exporter;
static struct zxdg_importer_v2* importer;
static struct wl_shm* shm2;
static struct xdg_toplevel_icon_manager_v1* icons;
static int icon_size;

static void icons_size(void* d, struct xdg_toplevel_icon_manager_v1* m, int32_t size) {
    (void)d; (void)m;
    if (!icon_size) icon_size = size;
}
static void icons_done(void* d, struct xdg_toplevel_icon_manager_v1* m) { (void)d; (void)m; }
static const struct xdg_toplevel_icon_manager_v1_listener icons_listener = {icons_size, icons_done};

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    else if (!strcmp(iface, wp_tearing_control_manager_v1_interface.name))
        tearing = wl_registry_bind(registry, name, &wp_tearing_control_manager_v1_interface, 1);
    else if (!strcmp(iface, xdg_wm_dialog_v1_interface.name))
        dialogs = wl_registry_bind(registry, name, &xdg_wm_dialog_v1_interface, 1);
    else if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        decorations = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
    else if (!strcmp(iface, ext_data_control_manager_v1_interface.name))
        dc = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_tablet_manager_v2_interface.name))
        tablets = wl_registry_bind(registry, name, &zwp_tablet_manager_v2_interface, 1);
    else if (!strcmp(iface, xdg_toplevel_drag_manager_v1_interface.name))
        drags = wl_registry_bind(registry, name, &xdg_toplevel_drag_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_input_method_manager_v2_interface.name))
        ims = wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1);
    else if (!strcmp(iface, ext_foreign_toplevel_list_v1_interface.name))
        foreign_list_name = name;
    else if (!strcmp(iface, zwp_text_input_manager_v3_interface.name))
        text_inputs = wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1);
    else if (!strcmp(iface, zxdg_exporter_v2_interface.name))
        exporter = wl_registry_bind(registry, name, &zxdg_exporter_v2_interface, 1);
    else if (!strcmp(iface, zxdg_importer_v2_interface.name))
        importer = wl_registry_bind(registry, name, &zxdg_importer_v2_interface, 1);
    else if (!strcmp(iface, xdg_toplevel_icon_manager_v1_interface.name)) {
        icons = wl_registry_bind(registry, name, &xdg_toplevel_icon_manager_v1_interface, 1);
        xdg_toplevel_icon_manager_v1_add_listener(icons, &icons_listener, NULL);
    } else if (!strcmp(iface, wl_shm_interface.name) && version >= 2)
        shm2 = wl_registry_bind(registry, name, &wl_shm_interface, 2);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_registry* extra;

static void need(const void* global, const char* name) {
    if (!global) {
        fprintf(stderr, "no %s global\n", name);
        exit(2);
    }
}

static void roundtrip(const char* what) {
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "%s: the connection died\n", what);
        exit(1);
    }
}

// prints "step N" and blocks until the scenario presses KEY_1
static void step(int n) {
    roundtrip("step");
    printf("step %d\n", n);
    wlk_watch_key = KEY_1;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
}

static void idle(void) {
    while (wl_display_dispatch(wl_dpy) != -1) {
    }
}

// ---- set-parent: xdg_toplevel.set_parent, then back to none -----------------
static int mode_set_parent(void) {
    struct wl_toplevel_ctx a, b;

    wl_make_toplevel(&a, "misc-parent", 200, 150, 0xFF0000FF);
    wl_make_toplevel(&b, "misc-child", 120, 90, 0xFF00FF00);
    xdg_toplevel_set_parent(b.tl, a.tl);
    step(1);
    xdg_toplevel_set_parent(b.tl, NULL);
    step(2);
    idle();
    return 0;
}

// ---- viewport: destination unset, set again, then the viewport destroyed ----
static int mode_viewport(void) {
    need(viewporter, "wp_viewporter");

    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-viewport", 200, 160, 0xFF0000FF);

    struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, t.surface);

    wp_viewport_set_destination(vp, 100, 80);
    wl_surface_commit(t.surface);
    step(1); // 100x80
    wp_viewport_set_destination(vp, -1, -1);
    wl_surface_commit(t.surface);
    step(2); // back to the buffer's 200x160
    wp_viewport_set_destination(vp, 120, 96);
    wl_surface_commit(t.surface);
    step(3); // 120x96
    wp_viewport_destroy(vp);
    wl_surface_commit(t.surface);
    step(4); // the viewport's state goes with it: 200x160
    wp_viewporter_destroy(viewporter);
    roundtrip("viewporter destroy");
    printf("viewporter destroyed\n");
    idle();
    return 0;
}

// ---- tearing: an async hint, then the control destroyed ---------------------
static int mode_tearing(void) {
    need(tearing, "wp_tearing_control_manager_v1");

    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-tearing", 160, 120, 0xFF0000FF);

    struct wp_tearing_control_v1* tc = wp_tearing_control_manager_v1_get_tearing_control(tearing, t.surface);

    wp_tearing_control_v1_set_presentation_hint(tc, WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC);
    wl_surface_commit(t.surface);
    step(1); // tearing=1
    wp_tearing_control_v1_destroy(tc);
    wl_surface_commit(t.surface);
    step(2); // back to vsync
    idle();
    return 0;
}

// ---- dialog: a modal dialog whose xdg_dialog is destroyed first --------------
static int mode_dialog(void) {
    need(dialogs, "xdg_wm_dialog_v1");

    struct wl_toplevel_ctx parent, t;

    wl_make_toplevel(&parent, "misc-dialog-parent", 200, 150, 0xFF0000FF);
    wl_make_toplevel(&t, "misc-dialog", 120, 90, 0xFF00FF00);
    xdg_toplevel_set_parent(t.tl, parent.tl);

    struct xdg_dialog_v1* d = xdg_wm_dialog_v1_get_xdg_dialog(dialogs, t.tl);

    xdg_dialog_v1_set_modal(d);
    step(1); // modal=1
    xdg_dialog_v1_destroy(d);
    step(2); // modal=0, the toplevel stays
    idle();
    return 0;
}

// ---- decoration: the mode the policy answers, unset, then destroyed ----------
static uint32_t deco_mode, deco_configures;

static void deco_configure(void* d, struct zxdg_toplevel_decoration_v1* deco, uint32_t mode) {
    (void)d; (void)deco;
    deco_mode = mode;
    deco_configures++;
}
static const struct zxdg_toplevel_decoration_v1_listener deco_listener = {deco_configure};

static void expect_deco(uint32_t mode, const char* what) {
    uint32_t before = deco_configures;

    roundtrip(what);
    if (deco_configures == before || deco_mode != mode) {
        fprintf(stderr, "%s: decoration mode %u (%u configures), want %u\n", what, deco_mode,
                deco_configures - before, mode);
        exit(1);
    }
}

// argv: the policy the scenario set (client | preference)
static int mode_decoration(const char* policy) {
    need(decorations, "zxdg_decoration_manager_v1");

    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-deco", 160, 120, 0xFF0000FF);

    struct zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration(decorations, t.tl);

    zxdg_toplevel_decoration_v1_add_listener(deco, &deco_listener, NULL);
    zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

    int client = !strcmp(policy, "client");

    // the client policy overrides the request; the preference honours it
    expect_deco(client ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE,
                "set_mode server");
    zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    expect_deco(ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE, "set_mode client");
    // with nothing requested the preference falls back to server side
    zxdg_toplevel_decoration_v1_unset_mode(deco);
    expect_deco(client ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE,
                "unset_mode");
    printf("decoration %s ok\n", policy);
    step(1);
    // without the object the toplevel draws its own decorations again
    zxdg_toplevel_decoration_v1_destroy(deco);
    step(2);
    idle();
    return 0;
}

// ---- popups: unmapping a parent dismisses its popups ------------------------
static int popup_done_count[3];

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)p;
    popup_done_count[(intptr_t)d]++;
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

struct popup_ctx {
    struct wl_surface* surface;
    struct xdg_surface* xs;
    struct xdg_popup* popup;
    int configured;
};

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct popup_ctx* c = d;

    xdg_surface_ack_configure(xs, serial);
    c->configured = 1;
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static struct xdg_positioner* positioner(uint32_t parent_serial) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

    xdg_positioner_set_size(pos, 40, 30);
    xdg_positioner_set_anchor_rect(pos, 5, 5, 10, 10);
    // the v3 parent hints: the parent's size and the configure it describes
    xdg_positioner_set_parent_size(pos, 160, 120);
    xdg_positioner_set_parent_configure(pos, parent_serial);
    xdg_positioner_set_reactive(pos);
    return pos;
}

static void map_popup(struct popup_ctx* c, struct xdg_surface* parent, intptr_t index) {
    c->surface = wl_compositor_create_surface(wl_comp);
    c->xs = xdg_wm_base_get_xdg_surface(wl_wm, c->surface);
    xdg_surface_add_listener(c->xs, &popup_xs_listener, c);
    c->popup = xdg_surface_get_popup(c->xs, parent, positioner(0));
    xdg_popup_add_listener(c->popup, &popup_listener, (void*)index);
    wl_surface_commit(c->surface);
    while (!c->configured && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_surface_attach(c->surface, wl_solid(40, 30, 0xFFFFFF00), 0, 0);
    wl_surface_damage(c->surface, 0, 0, 40, 30);
    wl_surface_commit(c->surface);
    roundtrip("map popup");
}

static int mode_popups(void) {
    struct wl_toplevel_ctx t;
    struct popup_ctx p0 = {0}, p1 = {0}, p2 = {0};

    wl_make_toplevel(&t, "misc-popups", 160, 120, 0xFF0000FF);
    map_popup(&p0, t.xs, 0);
    map_popup(&p1, p0.xs, 1);
    // a popup that unmaps takes its mapped child popups with it
    wl_surface_attach(p0.surface, NULL, 0, 0);
    wl_surface_commit(p0.surface);
    roundtrip("unmap popup");
    if (popup_done_count[1] != 1) {
        fprintf(stderr, "the child of an unmapped popup was not dismissed (%d)\n", popup_done_count[1]);
        return 1;
    }

    // and a toplevel that unmaps takes its popups
    map_popup(&p2, t.xs, 2);
    wl_surface_attach(t.surface, NULL, 0, 0);
    wl_surface_commit(t.surface);
    roundtrip("unmap toplevel");
    if (popup_done_count[2] != 1) {
        fprintf(stderr, "the popup of an unmapped toplevel was not dismissed (%d)\n", popup_done_count[2]);
        return 1;
    }

    // with every xdg object gone, the wm_base itself may go
    xdg_popup_destroy(p2.popup);
    xdg_surface_destroy(p2.xs);
    xdg_popup_destroy(p1.popup);
    xdg_surface_destroy(p1.xs);
    xdg_popup_destroy(p0.popup);
    xdg_surface_destroy(p0.xs);
    xdg_toplevel_destroy(t.tl);
    xdg_surface_destroy(t.xs);
    xdg_wm_base_destroy(wl_wm);
    roundtrip("wm_base destroy");
    printf("popups ok\n");
    return 0;
}

// ---- suspended: a minimized toplevel is told it is suspended ----------------
static int suspended_seen, sus_configured;

static void sus_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* states) {
    (void)d; (void)t; (void)w; (void)h;
    uint32_t* s;

    suspended_seen = 0;
    wl_array_for_each(s, states) {
        if (*s == XDG_TOPLEVEL_STATE_SUSPENDED) {
            suspended_seen = 1;
        }
    }
}
static const struct xdg_toplevel_listener sus_listener = {
    .configure = sus_configure,
    .close = wl_tl_close,
    .configure_bounds = wl_tl_configure_bounds,
    .wm_capabilities = wl_tl_wm_capabilities,
};

static void sus_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    sus_configured++;
}
static const struct xdg_surface_listener sus_xs_listener = {sus_xs_configure};

static int mode_suspended(void) {
    struct wl_surface* s = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, s);

    xdg_surface_add_listener(xs, &sus_xs_listener, NULL);

    struct xdg_toplevel* t = xdg_surface_get_toplevel(xs);

    xdg_toplevel_add_listener(t, &sus_listener, NULL);
    xdg_toplevel_set_app_id(t, "misc-suspended");
    wl_surface_commit(s);
    while (!sus_configured && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_surface_attach(s, wl_solid(120, 90, 0xFF0000FF), 0, 0);
    wl_surface_commit(s);
    roundtrip("map");

    int before = sus_configured;

    xdg_toplevel_set_minimized(t);
    for (int i = 0; i < 100 && (sus_configured == before || !suspended_seen); i++) {
        roundtrip("minimize");
        usleep(10000);
    }
    if (!suspended_seen) {
        fprintf(stderr, "a minimized v6 toplevel was not suspended\n");
        return 1;
    }
    printf("suspended ok\n");
    return 0;
}

// ---- foreign-list: bound after a toplevel mapped, then stopped ---------------
static int handles, finished;

static void list_toplevel(void* d, struct ext_foreign_toplevel_list_v1* l, struct ext_foreign_toplevel_handle_v1* h) {
    (void)d; (void)l; (void)h;
    handles++;
}
static void list_finished(void* d, struct ext_foreign_toplevel_list_v1* l) {
    (void)d; (void)l;
    finished = 1;
}
static const struct ext_foreign_toplevel_list_v1_listener list_listener = {list_toplevel, list_finished};

static int mode_foreign_list(void) {
    need((void*)(uintptr_t)foreign_list_name, "ext_foreign_toplevel_list_v1");

    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-foreign", 120, 90, 0xFF0000FF);

    struct ext_foreign_toplevel_list_v1* list =
        wl_registry_bind(extra, foreign_list_name, &ext_foreign_toplevel_list_v1_interface, 1);

    ext_foreign_toplevel_list_v1_add_listener(list, &list_listener, NULL);
    roundtrip("bind list");
    if (handles < 1) {
        fprintf(stderr, "a list bound after the map announced no toplevel\n");
        return 1;
    }
    ext_foreign_toplevel_list_v1_stop(list);
    roundtrip("stop");
    if (!finished) {
        fprintf(stderr, "stop was not answered with finished\n");
        return 1;
    }
    printf("foreign list ok\n");
    return 0;
}

// ---- tablet: the client destroys the tool and the tablet it was given ---------
static struct zwp_tablet_v2* tablet_obj;
static struct zwp_tablet_tool_v2* tool_obj;

static void seat_tablet_added(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_v2* t) {
    (void)d; (void)s;
    tablet_obj = t;
}
static void seat_tool_added(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_tool_v2* t) {
    (void)d; (void)s;
    tool_obj = t;
}
static void seat_pad_added(void* d, struct zwp_tablet_seat_v2* s, struct zwp_tablet_pad_v2* p) {
    (void)d; (void)s; (void)p;
}
static const struct zwp_tablet_seat_v2_listener tablet_seat_listener = {
    seat_tablet_added, seat_tool_added, seat_pad_added,
};

static int mode_tablet(void) {
    need(tablets, "zwp_tablet_manager_v2");

    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-tablet", 200, 150, 0xFF0000FF);

    struct zwp_tablet_seat_v2* ts = zwp_tablet_manager_v2_get_tablet_seat(tablets, wl_seat_g);

    zwp_tablet_seat_v2_add_listener(ts, &tablet_seat_listener, NULL);
    roundtrip("tablet seat");
    if (!tablet_obj || !tool_obj) {
        fprintf(stderr, "no tablet or tool announced\n");
        return 1;
    }
    zwp_tablet_tool_v2_set_cursor(tool_obj, 0, NULL, 0, 0);
    zwp_tablet_tool_v2_destroy(tool_obj);
    zwp_tablet_v2_destroy(tablet_obj);
    step(1); // the scenario drives the pen over the window now
    idle();
    return 0;
}

// ---- restack: place_above a sibling, place_below the parent ------------------
static int mode_restack(void) {
    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-restack", 300, 200, 0xFFFF0000);

    struct wl_surface* green = wl_compositor_create_surface(wl_comp);
    struct wl_surface* blue = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* gs = wl_subcompositor_get_subsurface(wl_subcomp, green, t.surface);
    struct wl_subsurface* bs = wl_subcompositor_get_subsurface(wl_subcomp, blue, t.surface);

    // the subcompositor is not needed once the subsurfaces exist
    wl_subcompositor_destroy(wl_subcomp);
    wl_subsurface_set_position(gs, 20, 20);
    wl_subsurface_set_position(bs, 60, 60);
    wl_surface_attach(green, wl_solid(100, 100, 0xFF00FF00), 0, 0);
    wl_surface_damage(green, 0, 0, 100, 100);
    wl_surface_commit(green);
    wl_surface_attach(blue, wl_solid(100, 100, 0xFF0000FF), 0, 0);
    wl_surface_damage(blue, 0, 0, 100, 100);
    wl_surface_commit(blue);
    wl_surface_commit(t.surface);
    step(1); // blue, the newer sibling, covers the overlap
    wl_subsurface_place_above(gs, blue);
    wl_surface_commit(t.surface);
    step(2); // green covers it
    wl_subsurface_place_below(bs, t.surface);
    wl_surface_commit(t.surface);
    step(3); // blue is under the opaque parent
    idle();
    return 0;
}

// ---- dc-receive: the data-control loopback offer delivers the payload --------
static const char kPayload[] = "misc-dc";
static struct ext_data_control_offer_v1* dc_offer;

static void dc_send(void* d, struct ext_data_control_source_v1* s, const char* mime, int32_t fd) {
    (void)d; (void)s; (void)mime;
    if (write(fd, kPayload, sizeof(kPayload) - 1) < 0)
        perror("write");
    close(fd);
}
static void dc_cancelled(void* d, struct ext_data_control_source_v1* s) {
    (void)d; (void)s;
}
static const struct ext_data_control_source_v1_listener dc_source_listener = {dc_send, dc_cancelled};

static void dc_data_offer(void* d, struct ext_data_control_device_v1* dev, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dev; (void)o;
}
static void dc_selection(void* d, struct ext_data_control_device_v1* dev, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dev;
    dc_offer = o;
}
static void dc_finished(void* d, struct ext_data_control_device_v1* dev) {
    (void)d; (void)dev;
}
static void dc_primary(void* d, struct ext_data_control_device_v1* dev, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dev; (void)o;
}
static const struct ext_data_control_device_v1_listener dc_device_listener = {
    dc_data_offer, dc_selection, dc_finished, dc_primary,
};

static int mode_dc_receive(void) {
    need(dc, "ext_data_control_manager_v1");

    struct ext_data_control_device_v1* dev = ext_data_control_manager_v1_get_data_device(dc, wl_seat_g);

    ext_data_control_device_v1_add_listener(dev, &dc_device_listener, NULL);
    roundtrip("device");

    struct ext_data_control_source_v1* src = ext_data_control_manager_v1_create_data_source(dc);

    ext_data_control_source_v1_add_listener(src, &dc_source_listener, NULL);
    ext_data_control_source_v1_offer(src, "text/plain");
    ext_data_control_device_v1_set_selection(dev, src);
    roundtrip("set selection");
    if (!dc_offer) {
        fprintf(stderr, "no loopback offer\n");
        return 1;
    }

    int fds[2];

    if (pipe(fds) < 0) return 2;
    ext_data_control_offer_v1_receive(dc_offer, "text/plain", fds[1]);
    close(fds[1]);
    roundtrip("receive");

    char buf[64] = {0};
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);

    if (n < 0 || strcmp(buf, kPayload)) {
        fprintf(stderr, "payload mismatch: got \"%s\"\n", buf);
        return 1;
    }
    printf("dc receive ok\n");
    return 0;
}

// ---- toplevel-drag: a drag object destroyed before any drag started ----------
static int mode_toplevel_drag(void) {
    need(drags, "xdg_toplevel_drag_manager_v1");

    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
    struct xdg_toplevel_drag_v1* drag = xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag(drags, src);

    xdg_toplevel_drag_v1_destroy(drag);
    roundtrip("drag destroy");
    // the source is free for a fresh drag object afterwards
    drag = xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag(drags, src);
    xdg_toplevel_drag_v1_destroy(drag);
    wl_data_source_destroy(src);
    roundtrip("second drag");
    printf("toplevel drag ok\n");
    return 0;
}

// ---- im-grab: the input method releases its keyboard grab --------------------
static int mode_im_grab(void) {
    need(ims, "zwp_input_method_manager_v2");

    struct zwp_input_method_v2* im = zwp_input_method_manager_v2_get_input_method(ims, wl_seat_g);
    struct zwp_input_method_keyboard_grab_v2* grab = zwp_input_method_v2_grab_keyboard(im);

    roundtrip("grab");
    zwp_input_method_keyboard_grab_v2_release(grab);
    roundtrip("release");
    // a released grab can be taken again
    grab = zwp_input_method_v2_grab_keyboard(im);
    roundtrip("regrab");
    printf("im grab ok\n");
    return 0;
}


// ---- im-popup: the popup surface object destroyed before its input method ---
static int mode_im_popup(void) {
    need(ims, "zwp_input_method_manager_v2");

    struct zwp_input_method_v2* im = zwp_input_method_manager_v2_get_input_method(ims, wl_seat_g);
    struct wl_surface* s = wl_compositor_create_surface(wl_comp);
    struct zwp_input_popup_surface_v2* popup = zwp_input_method_v2_get_input_popup_surface(im, s);

    roundtrip("popup");
    zwp_input_popup_surface_v2_destroy(popup);
    roundtrip("popup destroy");
    // the input method outlives it and can wrap a surface again
    popup = zwp_input_method_v2_get_input_popup_surface(im, wl_compositor_create_surface(wl_comp));
    roundtrip("second popup");
    printf("im popup ok\n");
    return 0;
}

// ---- text-input: the active text input destroyed deactivates the method -----
static int im_active = -1, im_cause = -1, im_dones;
static int ti_entered;

static void im_activate(void* d, struct zwp_input_method_v2* im) { (void)d; (void)im; im_active = 1; }
static void im_deactivate(void* d, struct zwp_input_method_v2* im) { (void)d; (void)im; im_active = 0; }
static void im_surrounding(void* d, struct zwp_input_method_v2* im, const char* t, uint32_t c, uint32_t a) {
    (void)d; (void)im; (void)t; (void)c; (void)a;
}
static void im_change_cause(void* d, struct zwp_input_method_v2* im, uint32_t cause) {
    (void)d; (void)im;
    im_cause = (int)cause;
}
static void im_content_type(void* d, struct zwp_input_method_v2* im, uint32_t h, uint32_t p) {
    (void)d; (void)im; (void)h; (void)p;
}
static void im_done(void* d, struct zwp_input_method_v2* im) { (void)d; (void)im; im_dones++; }
static void im_unavailable(void* d, struct zwp_input_method_v2* im) {
    (void)d; (void)im;
    fprintf(stderr, "input method unavailable\n");
    exit(1);
}
static const struct zwp_input_method_v2_listener im_listener = {
    im_activate, im_deactivate, im_surrounding, im_change_cause, im_content_type, im_done, im_unavailable,
};

static void ti_enter(void* d, struct zwp_text_input_v3* ti, struct wl_surface* s) { (void)d; (void)ti; (void)s; ti_entered = 1; }
static void ti_leave(void* d, struct zwp_text_input_v3* ti, struct wl_surface* s) { (void)d; (void)ti; (void)s; }
static void ti_preedit(void* d, struct zwp_text_input_v3* ti, const char* t, int32_t b, int32_t e) {
    (void)d; (void)ti; (void)t; (void)b; (void)e;
}
static void ti_commit_string(void* d, struct zwp_text_input_v3* ti, const char* t) { (void)d; (void)ti; (void)t; }
static void ti_delete(void* d, struct zwp_text_input_v3* ti, uint32_t b, uint32_t a) { (void)d; (void)ti; (void)b; (void)a; }
static void ti_done(void* d, struct zwp_text_input_v3* ti, uint32_t serial) { (void)d; (void)ti; (void)serial; }
static const struct zwp_text_input_v3_listener ti_listener = {
    ti_enter, ti_leave, ti_preedit, ti_commit_string, ti_delete, ti_done,
};

static int mode_text_input(void) {
    need(ims, "zwp_input_method_manager_v2");
    need(text_inputs, "zwp_text_input_manager_v3");

    struct zwp_input_method_v2* im = zwp_input_method_manager_v2_get_input_method(ims, wl_seat_g);

    zwp_input_method_v2_add_listener(im, &im_listener, NULL);

    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-text-input", 160, 120, 0xFF0000FF);

    struct zwp_text_input_v3* ti = zwp_text_input_manager_v3_get_text_input(text_inputs, wl_seat_g);

    zwp_text_input_v3_add_listener(ti, &ti_listener, NULL);
    for (int i = 0; i < 100 && !ti_entered; i++) {
        roundtrip("enter");
        usleep(10000);
    }
    if (!ti_entered) {
        fprintf(stderr, "the text input never entered the focused surface\n");
        return 1;
    }
    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_text_change_cause(ti, ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
    zwp_text_input_v3_commit(ti);
    roundtrip("enable");
    roundtrip("enable");
    if (im_active != 1 || im_cause != ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER) {
        fprintf(stderr, "enable: input method active=%d cause=%d\n", im_active, im_cause);
        return 1;
    }

    int dones = im_dones;

    // the active text input going away deactivates the input method
    zwp_text_input_v3_destroy(ti);
    roundtrip("destroy");
    roundtrip("destroy");
    if (im_active != 0 || im_dones == dones) {
        fprintf(stderr, "destroy: input method active=%d\n", im_active);
        return 1;
    }
    printf("text input ok\n");
    return 0;
}

// ---- foreign-gone: an exported toplevel destroyed under its import -----------
static char export_handle[128];
static int import_destroyed;

static void exported_handle(void* d, struct zxdg_exported_v2* e, const char* handle) {
    (void)d; (void)e;
    snprintf(export_handle, sizeof(export_handle), "%s", handle);
}
static const struct zxdg_exported_v2_listener exported_listener = {exported_handle};

static void imported_destroyed(void* d, struct zxdg_imported_v2* i) {
    (void)d; (void)i;
    import_destroyed = 1;
}
static const struct zxdg_imported_v2_listener imported_listener = {imported_destroyed};

static int mode_foreign_gone(void) {
    need(exporter, "zxdg_exporter_v2");
    need(importer, "zxdg_importer_v2");

    struct wl_toplevel_ctx parent, child;

    wl_make_toplevel(&parent, "misc-foreign-parent", 160, 120, 0xFF0000FF);
    wl_make_toplevel(&child, "misc-foreign-child", 120, 90, 0xFF00FF00);

    struct zxdg_exported_v2* ex = zxdg_exporter_v2_export_toplevel(exporter, parent.surface);

    zxdg_exported_v2_add_listener(ex, &exported_listener, NULL);
    roundtrip("export");
    if (!export_handle[0]) {
        fprintf(stderr, "no export handle\n");
        return 1;
    }

    struct zxdg_imported_v2* im = zxdg_importer_v2_import_toplevel(importer, export_handle);

    zxdg_imported_v2_add_listener(im, &imported_listener, NULL);
    zxdg_imported_v2_set_parent_of(im, child.surface);
    roundtrip("import");
    if (import_destroyed) {
        fprintf(stderr, "a live export's import was destroyed\n");
        return 1;
    }

    // the exported toplevel goes: its import is told, and parents nothing
    xdg_toplevel_destroy(parent.tl);
    xdg_surface_destroy(parent.xs);
    wl_surface_destroy(parent.surface);
    roundtrip("toplevel destroy");
    if (!import_destroyed) {
        fprintf(stderr, "the import outlived its exported toplevel\n");
        return 1;
    }
    zxdg_imported_v2_set_parent_of(im, child.surface);
    roundtrip("set_parent_of a dead import");
    printf("foreign gone ok\n");
    return 0;
}

// ---- foreign-bad-parent: set_parent_of a surface that is no toplevel ---------
static int mode_foreign_bad_parent(void) {
    need(exporter, "zxdg_exporter_v2");
    need(importer, "zxdg_importer_v2");

    struct wl_toplevel_ctx parent;

    wl_make_toplevel(&parent, "misc-foreign-parent", 160, 120, 0xFF0000FF);

    struct zxdg_exported_v2* ex = zxdg_exporter_v2_export_toplevel(exporter, parent.surface);

    zxdg_exported_v2_add_listener(ex, &exported_listener, NULL);
    roundtrip("export");

    struct zxdg_imported_v2* im = zxdg_importer_v2_import_toplevel(importer, export_handle);

    zxdg_imported_v2_set_parent_of(im, wl_compositor_create_surface(wl_comp));
    if (wl_expect_error("zxdg_imported_v2", ZXDG_IMPORTED_V2_ERROR_INVALID_SURFACE)) {
        return 1;
    }
    printf("foreign bad parent ok\n");
    return 0;
}

// ---- shm: a pool resized to its own size, and wl_shm.release (v2) -----------
static int mode_shm(void) {
    need(shm2, "wl_shm v2");

    int fd = memfd_create("misc-shm", 0);

    if (fd < 0 || ftruncate(fd, 64 * 64 * 4) < 0) return 2;

    struct wl_shm_pool* pool = wl_shm_create_pool(shm2, fd, 64 * 64 * 4);

    close(fd);
    // resizing to the current size is allowed and changes nothing
    wl_shm_pool_resize(pool, 64 * 64 * 4);

    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, 64, 64, 256, WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    // the pool and its buffer outlive the released wl_shm
    wl_shm_release(shm2);

    struct wl_surface* s = wl_compositor_create_surface(wl_comp);

    wl_surface_attach(s, buf, 0, 0);
    wl_surface_commit(s);
    roundtrip("shm");
    printf("shm ok\n");
    return 0;
}

// ---- input-region: an empty input region, then none (the whole surface) -----
static int mode_input_region(void) {
    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-input", 200, 150, 0xFF0000FF);

    struct wl_region* empty = wl_compositor_create_region(wl_comp);

    // degenerate rectangles add and take nothing; one reaching past the
    // coordinate range is clamped, and lies far off the window anyway
    wl_region_add(empty, 10, 10, 0, 20);
    wl_region_subtract(empty, 10, 10, 20, -1);
    wl_region_add(empty, INT32_MAX - 10, INT32_MAX - 10, 100, 100);
    wl_surface_set_input_region(t.surface, empty);
    wl_region_destroy(empty);
    // an opaque region set and taken back again
    struct wl_region* opaque = wl_compositor_create_region(wl_comp);

    wl_region_add(opaque, 0, 0, 200, 150);
    wl_surface_set_opaque_region(t.surface, opaque);
    wl_region_destroy(opaque);
    wl_surface_set_opaque_region(t.surface, NULL);
    wl_surface_commit(t.surface);
    step(1); // the scenario hovers the window: nothing to enter

    int before = wlp_enter_count;

    printf("enters %d\n", before);
    wl_surface_set_input_region(t.surface, NULL);
    wl_surface_commit(t.surface);
    step(2); // hovered again: the whole surface takes input
    printf("enters %d\n", wlp_enter_count);
    if (before != 0 || wlp_enter_count == 0) {
        fprintf(stderr, "input region: %d enters while empty, %d after reset\n", before, wlp_enter_count);
        return 1;
    }
    printf("input region ok\n");
    idle();
    return 0;
}

// ---- release: a second get_release before commit replaces the first --------
static int release_done[2];

static void release_cb(void* d, struct wl_callback* cb, uint32_t t) {
    (void)cb; (void)t;
    release_done[(intptr_t)d]++;
}
static const struct wl_callback_listener release_listener = {release_cb};

static int mode_release(void) {
    struct wl_surface* s = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub;
    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-release", 120, 90, 0xFF0000FF);
    sub = wl_subcompositor_get_subsurface(wl_subcomp, s, t.surface);
    wl_subsurface_set_desync(sub);
    wl_surface_attach(s, wl_solid(40, 40, 0xFF00FF00), 0, 0);

    struct wl_callback* first = wl_surface_get_release(s);
    struct wl_callback* second = wl_surface_get_release(s);

    wl_callback_add_listener(first, &release_listener, (void*)0);
    wl_callback_add_listener(second, &release_listener, (void*)1);
    wl_surface_commit(s);
    roundtrip("commit");
    // the next buffer releases the first one
    wl_surface_attach(s, wl_solid(40, 40, 0xFF0000FF), 0, 0);
    wl_surface_commit(s);
    for (int i = 0; i < 100 && !release_done[1]; i++) {
        roundtrip("release");
        usleep(10000);
    }
    if (release_done[0] || release_done[1] != 1) {
        fprintf(stderr, "release callbacks: replaced %d, kept %d\n", release_done[0], release_done[1]);
        return 1;
    }
    printf("release ok\n");
    return 0;
}

// ---- icon-twice: a second pixel icon retires the first ----------------------
static void set_pixel_icon(struct wl_toplevel_ctx* t, uint32_t color) {
    int size = icon_size > 0 ? icon_size : 48;
    struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icons);

    xdg_toplevel_icon_v1_add_buffer(icon, wl_solid(size, size, color), 1);
    xdg_toplevel_icon_manager_v1_set_icon(icons, t->tl, icon);
    xdg_toplevel_icon_v1_destroy(icon);
    wl_surface_commit(t->surface);
}

static int mode_icon_twice(void) {
    need(icons, "xdg_toplevel_icon_manager_v1");
    roundtrip("icon size");

    struct wl_toplevel_ctx t;

    wl_make_toplevel(&t, "misc-icon", 160, 120, 0xFF0000FF);
    set_pixel_icon(&t, 0xFF00FF00);
    step(1);
    set_pixel_icon(&t, 0xFFFF00FF);
    step(2);
    idle();
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (argc < 2) return 2;
    if (wl_boot()) return 2;
    extra = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(extra, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    const char* mode = argv[1];

    if (!strcmp(mode, "set-parent")) return mode_set_parent();
    if (!strcmp(mode, "viewport")) return mode_viewport();
    if (!strcmp(mode, "tearing")) return mode_tearing();
    if (!strcmp(mode, "dialog")) return mode_dialog();
    if (!strcmp(mode, "decoration") && argc == 3) return mode_decoration(argv[2]);
    if (!strcmp(mode, "popups")) return mode_popups();
    if (!strcmp(mode, "suspended")) return mode_suspended();
    if (!strcmp(mode, "foreign-list")) return mode_foreign_list();
    if (!strcmp(mode, "tablet")) return mode_tablet();
    if (!strcmp(mode, "restack")) return mode_restack();
    if (!strcmp(mode, "dc-receive")) return mode_dc_receive();
    if (!strcmp(mode, "toplevel-drag")) return mode_toplevel_drag();
    if (!strcmp(mode, "im-grab")) return mode_im_grab();
    if (!strcmp(mode, "im-popup")) return mode_im_popup();
    if (!strcmp(mode, "text-input")) return mode_text_input();
    if (!strcmp(mode, "foreign-gone")) return mode_foreign_gone();
    if (!strcmp(mode, "foreign-bad-parent")) return mode_foreign_bad_parent();
    if (!strcmp(mode, "shm")) return mode_shm();
    if (!strcmp(mode, "input-region")) return mode_input_region();
    if (!strcmp(mode, "release")) return mode_release();
    if (!strcmp(mode, "icon-twice")) return mode_icon_twice();
    fprintf(stderr, "unknown mode %s\n", mode);
    return 2;
}
