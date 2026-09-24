#include "wl_util.h"

#include <text-input-unstable-v3-client-protocol.h>
#include <input-method-unstable-v2-client-protocol.h>

// The input method's edges around its popup, one process playing both the
// application and the IME. The method binds and commits with no text input
// anywhere: nothing reaches anyone. Its popup surface exists before it has
// a buffer: it is told the cursor rectangle but not placed. The same
// rectangle committed again is not sent again. Once the popup has content
// it is placed in the scene, and disabling the text input takes it out.

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

static int ti_entered, ti_commits;

static void ti_enter(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
    ti_entered = 1;
}
static void ti_leave(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
    ti_entered = 0;
}
static void ti_preedit(void* d, struct zwp_text_input_v3* t, const char* text, int32_t a, int32_t b) {
    (void)d; (void)t; (void)text; (void)a; (void)b;
}
static void ti_commit_string(void* d, struct zwp_text_input_v3* t, const char* text) {
    (void)d; (void)t; (void)text;
    ti_commits++;
}
static void ti_delete(void* d, struct zwp_text_input_v3* t, uint32_t a, uint32_t b) {
    (void)d; (void)t; (void)a; (void)b;
}
static void ti_done(void* d, struct zwp_text_input_v3* t, uint32_t serial) {
    (void)d; (void)t; (void)serial;
}
static const struct zwp_text_input_v3_listener ti_listener = {
    .enter = ti_enter,
    .leave = ti_leave,
    .preedit_string = ti_preedit,
    .commit_string = ti_commit_string,
    .delete_surrounding_text = ti_delete,
    .done = ti_done,
};

static int im_active, im_activations, im_unavailable;

static void im_activate(void* d, struct zwp_input_method_v2* m) {
    (void)d; (void)m;
    im_active = 1;
    im_activations++;
}
static void im_deactivate(void* d, struct zwp_input_method_v2* m) {
    (void)d; (void)m;
    im_active = 0;
}
static void im_surrounding(void* d, struct zwp_input_method_v2* m, const char* t, uint32_t c, uint32_t a) {
    (void)d; (void)m; (void)t; (void)c; (void)a;
}
static void im_change_cause(void* d, struct zwp_input_method_v2* m, uint32_t c) {
    (void)d; (void)m; (void)c;
}
static void im_content_type(void* d, struct zwp_input_method_v2* m, uint32_t h, uint32_t p) {
    (void)d; (void)m; (void)h; (void)p;
}
static void im_done(void* d, struct zwp_input_method_v2* m) {
    (void)d; (void)m;
}
static void im_unavailable_cb(void* d, struct zwp_input_method_v2* m) {
    (void)d; (void)m;
    im_unavailable = 1;
}
static const struct zwp_input_method_v2_listener im_listener = {
    .activate = im_activate,
    .deactivate = im_deactivate,
    .surrounding_text = im_surrounding,
    .text_change_cause = im_change_cause,
    .content_type = im_content_type,
    .done = im_done,
    .unavailable = im_unavailable_cb,
};

static int rects;
static int32_t rect_x, rect_y, rect_w, rect_h;

static void popup_rect(void* d, struct zwp_input_popup_surface_v2* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p;
    rect_x = x;
    rect_y = y;
    rect_w = w;
    rect_h = h;
    rects++;
}
static const struct zwp_input_popup_surface_v2_listener popup_listener = {
    .text_input_rectangle = popup_rect,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!ti_mgr || !im_mgr || !seat2) {
        fprintf(stderr, "missing ime globals\n");
        return 2;
    }

    // the method before any application: nothing to activate, and what it
    // commits has no text input to go to
    struct zwp_input_method_v2* im = zwp_input_method_manager_v2_get_input_method(im_mgr, seat2);
    zwp_input_method_v2_add_listener(im, &im_listener, NULL);
    zwp_input_method_v2_commit_string(im, "lost");
    zwp_input_method_v2_commit(im, 0);
    wl_display_roundtrip(wl_dpy);

    if (im_unavailable || im_active) {
        fprintf(stderr, "an input method with nothing to type into is %s\n", im_unavailable ? "unavailable" : "active");
        return 1;
    }

    // the popup surface, with no buffer yet
    struct wl_surface* popup_surface = wl_compositor_create_surface(wl_comp);
    struct zwp_input_popup_surface_v2* popup = zwp_input_method_v2_get_input_popup_surface(im, popup_surface);
    zwp_input_popup_surface_v2_add_listener(popup, &popup_listener, NULL);

    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "ime-popup-target", 240, 160, 0xff304050);

    struct zwp_text_input_v3* ti = zwp_text_input_manager_v3_get_text_input(ti_mgr, seat2);
    zwp_text_input_v3_add_listener(ti, &ti_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!ti_entered) {
        fprintf(stderr, "text input never entered the focused surface\n");
        return 1;
    }

    if (ti_commits) {
        fprintf(stderr, "the commit made with no text input reached one later\n");
        return 1;
    }

    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_cursor_rectangle(ti, 20, 30, 4, 12);
    zwp_text_input_v3_commit(ti);

    while ((!im_active || !rects) && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (rect_x != -20 || rect_y != -30 || rect_w != 4 || rect_h != 12) {
        fprintf(stderr, "the popup was told %d,%d %dx%d\n", rect_x, rect_y, rect_w, rect_h);
        return 1;
    }

    // the same rectangle again: nothing new to say
    zwp_text_input_v3_set_cursor_rectangle(ti, 20, 30, 4, 12);
    zwp_text_input_v3_commit(ti);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (rects != 1) {
        fprintf(stderr, "an unchanged rectangle was sent again (%d)\n", rects);
        return 1;
    }

    printf("popup without content\n");

    if (wl_await_file("go-content")) {
        return 1;
    }

    // content for the popup: the scene places it under the cursor
    wl_surface_attach(popup_surface, wl_solid(60, 20, 0xffff8000), 0, 0);
    wl_surface_damage_buffer(popup_surface, 0, 0, 60, 20);
    wl_surface_commit(popup_surface);
    wl_display_roundtrip(wl_dpy);
    zwp_text_input_v3_set_cursor_rectangle(ti, 24, 30, 4, 12);
    zwp_text_input_v3_commit(ti);

    while (rects < 2 && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("popup shown\n");

    if (wl_await_file("go-disable")) {
        return 1;
    }

    // disabled: the method deactivates and the popup leaves the scene
    zwp_text_input_v3_disable(ti);
    zwp_text_input_v3_commit(ti);

    while (im_active && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_display_roundtrip(wl_dpy);
    printf("popup hidden\n");

    if (wl_await_file("go-quit")) {
        return 1;
    }

    return 0;
}
