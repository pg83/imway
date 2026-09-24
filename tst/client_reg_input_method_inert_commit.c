#include "wl_util.h"

#include <text-input-unstable-v3-client-protocol.h>
#include <input-method-unstable-v2-client-protocol.h>

// Two input methods on one seat: the first is active on a focused text
// input and has staged a string it has not committed yet; the second, the
// inert one, commits. Its commit is a request of an inert object and must
// do nothing: the staged string reaches the text input only once the
// first method commits it.

static struct zwp_text_input_manager_v3* ti_mgr;
static struct zwp_input_method_manager_v2* im_mgr;
static struct wl_seat* seat2;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
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

static int ti_entered, commits;
static char committed[64];

static void ti_enter(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
    ti_entered = 1;
}
static void ti_leave(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
}
static void ti_preedit(void* d, struct zwp_text_input_v3* t, const char* text, int32_t a, int32_t b) {
    (void)d; (void)t; (void)text; (void)a; (void)b;
}
static void ti_commit_string(void* d, struct zwp_text_input_v3* t, const char* text) {
    (void)d; (void)t;
    snprintf(committed, sizeof(committed), "%s", text ? text : "");
    commits++;
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

// d: 0 for the first method, 1 for the second
static int active[2], unavailable[2];

static void im_activate(void* d, struct zwp_input_method_v2* m) {
    (void)m;
    active[d ? 1 : 0] = 1;
}
static void im_deactivate(void* d, struct zwp_input_method_v2* m) {
    (void)m;
    active[d ? 1 : 0] = 0;
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
static void im_unavailable(void* d, struct zwp_input_method_v2* m) {
    (void)m;
    unavailable[d ? 1 : 0] = 1;
}
static const struct zwp_input_method_v2_listener im_listener = {
    .activate = im_activate,
    .deactivate = im_deactivate,
    .surrounding_text = im_surrounding,
    .text_change_cause = im_change_cause,
    .content_type = im_content_type,
    .done = im_done,
    .unavailable = im_unavailable,
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

    struct zwp_input_method_v2* first = zwp_input_method_manager_v2_get_input_method(im_mgr, seat2);
    zwp_input_method_v2_add_listener(first, &im_listener, (void*)0);
    struct zwp_input_method_v2* second = zwp_input_method_manager_v2_get_input_method(im_mgr, seat2);
    zwp_input_method_v2_add_listener(second, &im_listener, (void*)1);
    wl_display_roundtrip(wl_dpy);

    if (unavailable[0] || !unavailable[1]) {
        fprintf(stderr, "the methods came up as %d/%d unavailable, not the second alone\n", unavailable[0], unavailable[1]);
        return 1;
    }

    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "inert-commit-target", 200, 120, 0xff203040);

    struct zwp_text_input_v3* ti = zwp_text_input_manager_v3_get_text_input(ti_mgr, seat2);
    zwp_text_input_v3_add_listener(ti, &ti_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!ti_entered) {
        fprintf(stderr, "text input never entered the focused surface\n");
        return 1;
    }

    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_commit(ti);
    while (!active[0] && wl_display_dispatch(wl_dpy) != -1) {
    }

    // the first method stages a string; the inert second commits
    zwp_input_method_v2_commit_string(first, "early");
    zwp_input_method_v2_commit(second, 0);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (commits) {
        fprintf(stderr, "the inert method's commit delivered the active one's \"%s\"\n", committed);
        return 1;
    }

    // the first method's own commit delivers it
    zwp_input_method_v2_commit(first, 0);
    while (!commits && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (strcmp(committed, "early")) {
        fprintf(stderr, "the first method's commit delivered \"%s\"\n", committed);
        return 1;
    }

    printf("inert commit done\n");
    return 0;
}
