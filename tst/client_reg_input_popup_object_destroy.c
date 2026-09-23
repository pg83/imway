#include "wl_util.h"

#include <text-input-unstable-v3-client-protocol.h>
#include <input-method-unstable-v2-client-protocol.h>

// The input method destroys the zwp_input_popup_surface_v2 of a popup that is
// on screen while it, the text input and the wl_surface all live on: the
// compositor must take the popup off the screen. The scenario watches the
// state dump's ime popup line and says when to destroy (a marker file).

static struct zwp_text_input_manager_v3* ti_mgr;
static struct zwp_input_method_manager_v2* im_mgr;
static struct wl_seat* seat2;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, zwp_text_input_manager_v3_interface.name))
        ti_mgr = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
    else if (!strcmp(iface, zwp_input_method_manager_v2_interface.name))
        im_mgr = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat2)
        seat2 = wl_registry_bind(r, name, &wl_seat_interface, 5);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int ti_entered;
static void ti_enter(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s; ti_entered = 1;
}
static void ti_leave(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s; ti_entered = 0;
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

static void popup_rect(void* d, struct zwp_input_popup_surface_v2* p,
                       int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static const struct zwp_input_popup_surface_v2_listener popup_listener = {popup_rect};

static void wait_marker(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", getenv("XDG_RUNTIME_DIR"), name);
    while (access(path, F_OK) != 0) {
        wl_display_roundtrip(wl_dpy);
        usleep(20000);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!ti_mgr || !im_mgr || !seat2) return 2;

    struct zwp_input_method_v2* im = zwp_input_method_manager_v2_get_input_method(im_mgr, seat2);
    zwp_input_method_v2_add_listener(im, &im_listener, NULL);

    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "input-popup-object-app", 300, 300, 0xff203040);

    struct zwp_text_input_v3* ti = zwp_text_input_manager_v3_get_text_input(ti_mgr, seat2);
    zwp_text_input_v3_add_listener(ti, &ti_listener, NULL);
    for (int i = 0; i < 200 && !ti_entered; i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(10000);
    }
    if (!ti_entered) { fprintf(stderr, "no enter\n"); return 1; }

    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_cursor_rectangle(ti, 40, 60, 8, 16);
    zwp_text_input_v3_commit(ti);
    wl_display_roundtrip(wl_dpy);
    while (!im_active && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wl_surface* surf = wl_compositor_create_surface(wl_comp);
    struct zwp_input_popup_surface_v2* popup =
        zwp_input_method_v2_get_input_popup_surface(im, surf);
    zwp_input_popup_surface_v2_add_listener(popup, &popup_listener, NULL);
    wl_surface_attach(surf, wl_solid(80, 24, 0xffffffff), 0, 0);
    wl_surface_damage(surf, 0, 0, 80, 24);
    wl_surface_commit(surf);
    wl_display_roundtrip(wl_dpy);
    printf("popup committed\n");

    wait_marker("go-destroy-popup");
    // only the role object goes; the wl_surface and the input method stay
    zwp_input_popup_surface_v2_destroy(popup);
    wl_display_roundtrip(wl_dpy);
    printf("popup object destroyed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
