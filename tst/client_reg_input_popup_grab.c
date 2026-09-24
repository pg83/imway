#include "wl_util.h"

#include <text-input-unstable-v3-client-protocol.h>
#include <input-method-unstable-v2-client-protocol.h>

// The text field sits in a grabbing xdg_popup (a search box in a menu): the
// grab takes the keyboard, and with it the text input, to the popup. The
// input method's popup is then placed under the cursor rectangle of the
// popup's surface, not of the toplevel the popup hangs off. The scenario
// reads both places from the state dump.

static struct zwp_text_input_manager_v3* ti_mgr;
static struct zwp_input_method_manager_v2* im_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, zwp_text_input_manager_v3_interface.name))
        ti_mgr = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
    else if (!strcmp(iface, zwp_input_method_manager_v2_interface.name))
        im_mgr = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_surface* ti_focus;
static void ti_enter(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; ti_focus = s;
}
static void ti_leave(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t;
    if (ti_focus == s) ti_focus = NULL;
}
static void ti_preedit(void* d, struct zwp_text_input_v3* t, const char* x, int32_t a, int32_t b) {
    (void)d; (void)t; (void)x; (void)a; (void)b;
}
static void ti_commit_string(void* d, struct zwp_text_input_v3* t, const char* x) {
    (void)d; (void)t; (void)x;
}
static void ti_delete(void* d, struct zwp_text_input_v3* t, uint32_t a, uint32_t b) {
    (void)d; (void)t; (void)a; (void)b;
}
static void ti_done(void* d, struct zwp_text_input_v3* t, uint32_t s) {
    (void)d; (void)t; (void)s;
}
static const struct zwp_text_input_v3_listener ti_listener = {
    ti_enter, ti_leave, ti_preedit, ti_commit_string, ti_delete, ti_done,
};

static int im_active;
static void im_activate(void* d, struct zwp_input_method_v2* m) { (void)d; (void)m; im_active = 1; }
static void im_deactivate(void* d, struct zwp_input_method_v2* m) { (void)d; (void)m; im_active = 0; }
static void im_surrounding(void* d, struct zwp_input_method_v2* m, const char* t, uint32_t c, uint32_t a) {
    (void)d; (void)m; (void)t; (void)c; (void)a;
}
static void im_change_cause(void* d, struct zwp_input_method_v2* m, uint32_t c) { (void)d; (void)m; (void)c; }
static void im_content_type(void* d, struct zwp_input_method_v2* m, uint32_t h, uint32_t p) {
    (void)d; (void)m; (void)h; (void)p;
}
static void im_done(void* d, struct zwp_input_method_v2* m) { (void)d; (void)m; }
static void im_unavailable(void* d, struct zwp_input_method_v2* m) { (void)d; (void)m; }
static const struct zwp_input_method_v2_listener im_listener = {
    im_activate, im_deactivate, im_surrounding, im_change_cause,
    im_content_type, im_done, im_unavailable,
};

static int rect_seen;
static void popup_rect(void* d, struct zwp_input_popup_surface_v2* p,
                       int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
    rect_seen++;
}
static const struct zwp_input_popup_surface_v2_listener ime_popup_listener = {popup_rect};

static struct wl_surface* menu_surface;
static struct xdg_popup* menu;
static int menu_committed;

static void menu_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void menu_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void menu_reposition(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener menu_listener = {menu_configure, menu_done, menu_reposition};

static void menu_xdg_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!menu_committed) {
        wl_surface_attach(menu_surface, wl_solid(160, 100, 0xFF00C0C0), 0, 0);
        wl_surface_commit(menu_surface);
        menu_committed = 1;
    }
}
static const struct xdg_surface_listener menu_xdg_listener = {menu_xdg_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!ti_mgr || !im_mgr || !wl_ptr) return 2;

    struct zwp_input_method_v2* im = zwp_input_method_manager_v2_get_input_method(im_mgr, wl_seat_g);
    zwp_input_method_v2_add_listener(im, &im_listener, NULL);

    struct wl_toplevel_ctx app;
    wl_make_toplevel(&app, "ime-grab-app", 400, 300, 0xFFFF0000);

    struct zwp_text_input_v3* ti = zwp_text_input_manager_v3_get_text_input(ti_mgr, wl_seat_g);
    zwp_text_input_v3_add_listener(ti, &ti_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    printf("ime-grab mapped\n");

    // the scenario's click gives the serial the grab needs
    while (!(wlp_button_count > 0 && wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED) && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 160, 100);
    xdg_positioner_set_anchor_rect(pos, 30, 30, 60, 20);
    menu_surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* mxs = xdg_wm_base_get_xdg_surface(wl_wm, menu_surface);
    xdg_surface_add_listener(mxs, &menu_xdg_listener, NULL);
    menu = xdg_surface_get_popup(mxs, app.xs, pos);
    xdg_popup_add_listener(menu, &menu_listener, NULL);
    xdg_popup_grab(menu, wl_seat_g, wlp_button_serial);
    xdg_positioner_destroy(pos);
    wl_surface_commit(menu_surface);

    // the grab moves the text input's focus to the menu
    while (!(menu_committed && ti_focus == menu_surface) && wl_display_dispatch(wl_dpy) != -1) {
    }

    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_cursor_rectangle(ti, 10, 12, 4, 14);
    zwp_text_input_v3_commit(ti);
    while (!im_active && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wl_surface* ime = wl_compositor_create_surface(wl_comp);
    struct zwp_input_popup_surface_v2* ime_popup = zwp_input_method_v2_get_input_popup_surface(im, ime);
    zwp_input_popup_surface_v2_add_listener(ime_popup, &ime_popup_listener, NULL);
    wl_surface_attach(ime, wl_solid(80, 24, 0xffffffff), 0, 0);
    wl_surface_damage(ime, 0, 0, 80, 24);
    wl_surface_commit(ime);
    while (!rect_seen && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("ime-grab placed\n");
    alarm(0);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
