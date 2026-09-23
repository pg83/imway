// xdg-shell, subsurface and surface requests on objects in the wrong state:
// each mode makes one mistake and must be told with the exact protocol error
// (or, for a popup grab nothing authorizes, with popup_done). Each mode needs
// a fresh client because the error disconnects it.

// wl_surface v5: attach offsets moved to wl_surface.offset
#define REG_COMPOSITOR_VERSION 5

#include "wl_util.h"

static int popup_done_seen;
static int popup_configured;
// commit the acked popup once with no buffer before mapping it
static int popup_empty_first;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)d; (void)p;
    popup_done_seen = 1;
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static void popup_xdg_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    struct wl_surface* surface = d;

    xdg_surface_ack_configure(xs, serial);
    if (!popup_configured) {
        if (popup_empty_first)
            wl_surface_commit(surface);
        wl_surface_attach(surface, wl_solid(40, 40, 0xFF0000FFu), 0, 0);
        wl_surface_commit(surface);
        popup_configured = 1;
    }
}
static const struct xdg_surface_listener popup_xdg_listener = {popup_xdg_configure};

static uint32_t toplevel_serials[3];
static int toplevel_configures;

static void count_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d; (void)xs;
    if (toplevel_configures < 3)
        toplevel_serials[toplevel_configures] = serial;
    toplevel_configures++;
}
static const struct xdg_surface_listener count_listener = {count_configure};

static int32_t tl_cfg_w, tl_cfg_h;
static int tl_configures;

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* states) {
    (void)d; (void)t; (void)states;
    tl_cfg_w = w;
    tl_cfg_h = h;
    tl_configures++;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; }
static void tl_bounds(void* d, struct xdg_toplevel* t, int32_t w, int32_t h) { (void)d; (void)t; (void)w; (void)h; }
static void tl_caps(void* d, struct xdg_toplevel* t, struct wl_array* c) { (void)d; (void)t; (void)c; }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close, tl_bounds, tl_caps};

static struct xdg_positioner* positioner(void) {
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

    xdg_positioner_set_size(pos, 40, 40);
    xdg_positioner_set_anchor_rect(pos, 0, 0, 10, 10);

    return pos;
}

// a mapped popup on a mapped toplevel
static struct xdg_popup* mapped_popup(void) {
    static struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "xdg-edges", 200, 120, 0xFF00FF00u);

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    struct xdg_popup* popup = xdg_surface_get_popup(xs, top.xs, positioner());

    xdg_surface_add_listener(xs, &popup_xdg_listener, surface);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    wl_surface_commit(surface);

    while (!popup_configured && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_display_roundtrip(wl_dpy);

    return popup;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);

    if (argc != 2 || wl_boot()) return 2;

    const char* mode = argv[1];
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);

    if (!strcmp(mode, "attach-offset-y")) {
        // a vertical offset alone is as much an offset as a horizontal one
        wl_surface_attach(surface, wl_solid(20, 20, 0xFF00FF00u), 0, 5);
        return wl_expect_error(wl_surface_interface.name, WL_SURFACE_ERROR_INVALID_OFFSET);
    }

    if (!strcmp(mode, "max-under-min")) {
        // a max alone (min still 0x0), then a min alone with no width, then
        // a max below that min: only the last commit conflicts
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
        struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);

        xdg_toplevel_set_max_size(tl, 0, 0);
        wl_surface_commit(surface);
        xdg_toplevel_set_min_size(tl, 0, 100);
        wl_surface_commit(surface);
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "a min size without a max was refused\n");
            return 1;
        }
        xdg_toplevel_set_max_size(tl, 0, 50);
        wl_surface_commit(surface);
        return wl_expect_error(xdg_toplevel_interface.name, XDG_TOPLEVEL_ERROR_INVALID_SIZE);
    }

    if (!strcmp(mode, "fullscreen-dead-surface")) {
        // the window's wl_surface is gone: going fullscreen still answers
        // with the output's size, having no size of its own to park
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
        struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);

        xdg_toplevel_add_listener(tl, &tl_listener, NULL);
        wl_surface_commit(surface);
        while (tl_configures < 1 && wl_display_dispatch(wl_dpy) != -1) {
        }
        wl_surface_destroy(surface);
        xdg_toplevel_set_fullscreen(tl, NULL);
        while (tl_configures < 2 && wl_display_dispatch(wl_dpy) != -1) {
        }
        if (tl_configures < 2 || tl_cfg_w <= 0 || tl_cfg_h <= 0) {
            fprintf(stderr, "fullscreen without a surface configured %dx%d (%d configures)\n", tl_cfg_w, tl_cfg_h, tl_configures);
            return 1;
        }
        printf("fullscreen at %dx%d\n", tl_cfg_w, tl_cfg_h);
        return 0;
    }

    if (!strcmp(mode, "resize-edge-none")) {
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
        struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);

        xdg_toplevel_resize(tl, wl_seat_g, 0, XDG_TOPLEVEL_RESIZE_EDGE_NONE);
        return wl_expect_error(xdg_toplevel_interface.name, XDG_TOPLEVEL_ERROR_INVALID_RESIZE_EDGE);
    }

    if (!strcmp(mode, "ack-skipped-serial")) {
        // acking a newer configure drops the older ones: acking one of
        // them after is acking a serial the compositor no longer has
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
        struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);

        xdg_surface_add_listener(xs, &count_listener, NULL);
        wl_surface_commit(surface);
        while (toplevel_configures < 1 && wl_display_dispatch(wl_dpy) != -1) {
        }
        xdg_surface_ack_configure(xs, toplevel_serials[0]);
        wl_surface_attach(surface, wl_solid(200, 120, 0xFF00FF00u), 0, 0);
        wl_surface_commit(surface);
        xdg_toplevel_set_fullscreen(tl, NULL);
        while (toplevel_configures < 2 && wl_display_dispatch(wl_dpy) != -1) {
        }
        xdg_toplevel_unset_fullscreen(tl);
        while (toplevel_configures < 3 && wl_display_dispatch(wl_dpy) != -1) {
        }
        xdg_surface_ack_configure(xs, toplevel_serials[2]);
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "acking the newest configure was refused\n");
            return 1;
        }
        xdg_surface_ack_configure(xs, toplevel_serials[1]);
        return wl_expect_error(xdg_surface_interface.name, XDG_SURFACE_ERROR_INVALID_SERIAL);
    }

    if (!strcmp(mode, "parent-size-flat")) {
        xdg_positioner_set_parent_size(xdg_wm_base_create_positioner(wl_wm), 10, 0);
        return wl_expect_error(xdg_positioner_interface.name, XDG_POSITIONER_ERROR_INVALID_INPUT);
    }

    if (!strcmp(mode, "xdg-on-subsurface")) {
        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);

        wl_subcompositor_get_subsurface(wl_subcomp, surface, parent);
        xdg_wm_base_get_xdg_surface(wl_wm, surface);
        return wl_expect_error(xdg_wm_base_interface.name, XDG_WM_BASE_ERROR_ROLE);
    }

    if (!strcmp(mode, "xdg-on-shown-surface")) {
        wl_surface_attach(surface, wl_solid(20, 20, 0xFF00FF00u), 0, 0);
        wl_surface_commit(surface);
        wl_surface_attach(surface, NULL, 0, 0);
        xdg_wm_base_get_xdg_surface(wl_wm, surface);
        return wl_expect_error(xdg_wm_base_interface.name, XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE);
    }

    if (!strcmp(mode, "subsurface-twice")) {
        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);

        wl_subcompositor_get_subsurface(wl_subcomp, surface, parent);
        wl_subcompositor_get_subsurface(wl_subcomp, surface, parent);
        return wl_expect_error(wl_subcompositor_interface.name, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE);
    }

    if (!strcmp(mode, "toplevel-on-popup")) {
        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wl_wm, parent);
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

        xdg_surface_get_toplevel(parent_xs);
        xdg_surface_get_popup(xs, parent_xs, positioner());
        xdg_surface_get_toplevel(xs);
        return wl_expect_error(xdg_surface_interface.name, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED);
    }

    if (!strcmp(mode, "popup-twice")) {
        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wl_wm, parent);
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

        xdg_surface_get_toplevel(parent_xs);
        xdg_surface_get_popup(xs, parent_xs, positioner());
        xdg_surface_get_popup(xs, parent_xs, positioner());
        return wl_expect_error(xdg_surface_interface.name, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED);
    }

    if (!strcmp(mode, "toplevel-dead-surface")) {
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

        wl_surface_destroy(surface);
        xdg_surface_get_toplevel(xs);
        return wl_expect_error(xdg_surface_interface.name, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED);
    }

    if (!strcmp(mode, "popup-dead-surface")) {
        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wl_wm, parent);
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

        xdg_surface_get_toplevel(parent_xs);
        wl_surface_destroy(surface);
        xdg_surface_get_popup(xs, parent_xs, positioner());
        return wl_expect_error(xdg_surface_interface.name, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED);
    }

    if (!strcmp(mode, "popup-parent-dead-surface")) {
        // the parent has its role, but its wl_surface is gone
        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wl_wm, parent);
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

        xdg_surface_get_toplevel(parent_xs);
        wl_surface_destroy(parent);
        xdg_surface_get_popup(xs, parent_xs, positioner());
        return wl_expect_error(xdg_wm_base_interface.name, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT);
    }

    if (!strcmp(mode, "xdg-before-popup")) {
        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wl_wm, parent);
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

        xdg_surface_get_toplevel(parent_xs);
        xdg_surface_get_popup(xs, parent_xs, positioner());
        xdg_surface_destroy(xs);

        // the proxy is gone on this side, so the error names no interface
        const struct wl_interface* iface = NULL;
        uint32_t id = 0;

        if (wl_display_roundtrip(wl_dpy) >= 0 || wl_display_get_error(wl_dpy) != EPROTO ||
            wl_display_get_protocol_error(wl_dpy, &iface, &id) != XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT) {
            fprintf(stderr, "destroying an xdg_surface under its popup was not a defunct_role_object error\n");
            return 1;
        }

        return 0;
    }

    if (!strcmp(mode, "reposition-no-anchor")) {
        // sized but never anchored
        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wl_wm, parent);
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
        struct xdg_positioner* sized = xdg_wm_base_create_positioner(wl_wm);

        xdg_surface_get_toplevel(parent_xs);
        xdg_positioner_set_size(sized, 40, 40);
        xdg_popup_reposition(xdg_surface_get_popup(xs, parent_xs, positioner()), sized, 1);
        return wl_expect_error(xdg_wm_base_interface.name, XDG_WM_BASE_ERROR_INVALID_POSITIONER);
    }

    if (!strcmp(mode, "grab-mapped-popup")) {
        xdg_popup_grab(mapped_popup(), wl_seat_g, 0);
        return wl_expect_error(xdg_popup_interface.name, XDG_POPUP_ERROR_INVALID_GRAB);
    }

    if (!strcmp(mode, "grab-popup-mapped-late")) {
        // an acked commit with no buffer leaves the popup unmapped; the
        // buffer after it maps it, so the grab comes too late
        popup_empty_first = 1;
        xdg_popup_grab(mapped_popup(), wl_seat_g, 0);
        return wl_expect_error(xdg_popup_interface.name, XDG_POPUP_ERROR_INVALID_GRAB);
    }

    if (!strcmp(mode, "popup-on-unmapped-popup")) {
        // the parent popup exists but never mapped
        static struct wl_toplevel_ctx top;

        wl_make_toplevel(&top, "xdg-edges", 200, 120, 0xFF00FF00u);

        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wl_wm, parent);
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

        xdg_surface_get_popup(parent_xs, top.xs, positioner());
        xdg_surface_get_popup(xs, parent_xs, positioner());
        wl_surface_commit(surface);
        return wl_expect_error(xdg_wm_base_interface.name, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT);
    }

    if (!strcmp(mode, "popup-no-parent-commit")) {
        // a parentless popup is for another protocol to place; committed
        // as a plain xdg popup it has no parent to be shown against
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);

        xdg_surface_get_popup(xs, NULL, positioner());
        wl_surface_commit(surface);
        return wl_expect_error(xdg_wm_base_interface.name, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT);
    }

    if (!strcmp(mode, "grab-no-parent")) {
        // a parentless popup's grab with a serial nothing issued is not an
        // error: the popup is dismissed
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
        struct xdg_popup* popup = xdg_surface_get_popup(xs, NULL, positioner());

        xdg_popup_add_listener(popup, &popup_listener, NULL);
        xdg_popup_grab(popup, wl_seat_g, 12345);
        if (wl_display_roundtrip(wl_dpy) < 0 || !popup_done_seen) {
            fprintf(stderr, "the unauthorized grab: done=%d error=%d\n", popup_done_seen, wl_display_get_error(wl_dpy));
            return 1;
        }
        printf("popup dismissed\n");
        return 0;
    }

    return 2;
}
