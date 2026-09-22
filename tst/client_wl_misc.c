// Rarely used requests of the core, xdg-shell and satellite protocols, one
// mode per scenario. Modes that the scenario checks from the compositor's
// side print "step N" and wait for KEY_1 before the next step; the rest
// check the answer themselves and exit 0 on success.
//   usage: client_wl_misc MODE

#define REG_XDG_VERSION 6
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

static struct wp_viewporter* viewporter;
static struct wp_tearing_control_manager_v1* tearing;
static struct xdg_wm_dialog_v1* dialogs;
static struct zxdg_decoration_manager_v1* decorations;
static struct ext_data_control_manager_v1* dc;
static struct zwp_tablet_manager_v2* tablets;
static struct xdg_toplevel_drag_manager_v1* drags;
static struct zwp_input_method_manager_v2* ims;
static uint32_t foreign_list_name;

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
    fprintf(stderr, "unknown mode %s\n", mode);
    return 2;
}
