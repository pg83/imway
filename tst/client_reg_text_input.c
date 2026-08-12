#include "wl_util.h"

#include <text-input-unstable-v3-client-protocol.h>
#include <input-method-unstable-v2-client-protocol.h>

// #F-12/13: text-input-v3 <-> input-method-v2 bridge. One process plays both
// the application (text_input) and the IME (input_method). A focused,
// enabled text input must activate the IME; the IME's commit_string must
// come back to the text input as commit_string + done.

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

// ---- text input side ----
static int ti_entered, ti_commit_seen;
static char ti_commit[64];

static void ti_enter(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
    ti_entered = 1;
}
static void ti_leave(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
    ti_entered = 0;
}
static void ti_preedit(void* d, struct zwp_text_input_v3* t, const char* text,
                       int32_t a, int32_t b) {
    (void)d; (void)t; (void)text; (void)a; (void)b;
}
static void ti_commit_string(void* d, struct zwp_text_input_v3* t, const char* text) {
    (void)d; (void)t;
    if (text) {
        strncpy(ti_commit, text, sizeof(ti_commit) - 1);
        ti_commit_seen = 1;
    }
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

// ---- input method side ----
static int im_active, im_unavailable;

static void im_activate(void* d, struct zwp_input_method_v2* m) {
    (void)d; (void)m;
    im_active = 1;
}
static void im_deactivate(void* d, struct zwp_input_method_v2* m) {
    (void)d; (void)m;
    im_active = 0;
}
static void im_surrounding(void* d, struct zwp_input_method_v2* m,
                           const char* t, uint32_t c, uint32_t a) {
    (void)d; (void)m; (void)t; (void)c; (void)a;
}
static void im_change_cause(void* d, struct zwp_input_method_v2* m, uint32_t c) {
    (void)d; (void)m; (void)c;
}
static void im_content_type(void* d, struct zwp_input_method_v2* m,
                            uint32_t h, uint32_t p) {
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

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!ti_mgr || !im_mgr || !seat2) {
        fprintf(stderr, "missing ime globals (ti=%p im=%p seat=%p)\n",
                (void*)ti_mgr, (void*)im_mgr, (void*)seat2);
        return 2;
    }

    // the IME registers first
    struct zwp_input_method_v2* im = zwp_input_method_manager_v2_get_input_method(im_mgr, seat2);
    zwp_input_method_v2_add_listener(im, &im_listener, NULL);

    // a focused toplevel gives the text input a home
    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "text-input-target", 200, 200, 0xff004080);

    struct zwp_text_input_v3* ti = zwp_text_input_manager_v3_get_text_input(ti_mgr, seat2);
    zwp_text_input_v3_add_listener(ti, &ti_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (im_unavailable) {
        fprintf(stderr, "input method reported unavailable\n");
        return 1;
    }
    if (!ti_entered) {
        fprintf(stderr, "text input never entered the focused surface\n");
        return 1;
    }

    // the application enables input
    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_content_type(ti, 0, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    zwp_text_input_v3_commit(ti);
    wl_display_roundtrip(wl_dpy);

    while (!im_active && wl_display_dispatch(wl_dpy) != -1) {
    }

    // the IME commits a string
    zwp_input_method_v2_commit_string(im, "hi");
    zwp_input_method_v2_commit(im, 0);
    wl_display_roundtrip(wl_dpy);

    while (!ti_commit_seen && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (strcmp(ti_commit, "hi")) {
        fprintf(stderr, "commit mismatch: \"%s\"\n", ti_commit);
        return 1;
    }

    // disabling deactivates the IME
    zwp_text_input_v3_disable(ti);
    zwp_text_input_v3_commit(ti);
    wl_display_roundtrip(wl_dpy);
    while (im_active && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("text-input done\n");
    fflush(stdout);
    return 0;
}
