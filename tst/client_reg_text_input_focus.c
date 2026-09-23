// text-input-v3 and input-method-v2 around activation, focus and settings.
//   - surrounding text set in the enabling commit reaches the IME with the
//     activation; a later change cause other than the IME's is forwarded
//   - an IME popup that was shown is hidden again when input is disabled
//   - empty preedit and commit strings reach the application as null
//   - an IME commit with no enabled text input is dropped
//   - keyboard focus moving to another window deactivates the IME and
//     leaves the text input
//   - with the keyboard grabbed by the IME, a repeat or layout change in the
//     settings reaches the grab too (and a v3 wl_keyboard, which has no
//     repeat_info, gets none)
// One process plays the application and the IME.

#include "wl_util.h"

#include <text-input-unstable-v3-client-protocol.h>
#include <input-method-unstable-v2-client-protocol.h>

static struct zwp_text_input_manager_v3* ti_mgr;
static struct zwp_input_method_manager_v2* im_mgr;
static struct wl_seat* seat3;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, zwp_text_input_manager_v3_interface.name))
        ti_mgr = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
    else if (!strcmp(iface, zwp_input_method_manager_v2_interface.name))
        im_mgr = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat3)
        seat3 = wl_registry_bind(r, name, &wl_seat_interface, 3);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

// ---- the application's text input ----
static int ti_entered, ti_leaves, ti_null_preedits, ti_null_commits, ti_commits;

static void ti_enter(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
    ti_entered = 1;
}
static void ti_leave(void* d, struct zwp_text_input_v3* t, struct wl_surface* s) {
    (void)d; (void)t; (void)s;
    ti_entered = 0;
    ti_leaves++;
}
static void ti_preedit(void* d, struct zwp_text_input_v3* t, const char* text, int32_t a, int32_t b) {
    (void)d; (void)t; (void)a; (void)b;
    if (!text)
        ti_null_preedits++;
}
static void ti_commit_string(void* d, struct zwp_text_input_v3* t, const char* text) {
    (void)d; (void)t;
    ti_commits++;
    if (!text)
        ti_null_commits++;
}
static void ti_delete(void* d, struct zwp_text_input_v3* t, uint32_t a, uint32_t b) { (void)d; (void)t; (void)a; (void)b; }
static void ti_done(void* d, struct zwp_text_input_v3* t, uint32_t serial) { (void)d; (void)t; (void)serial; }
static const struct zwp_text_input_v3_listener ti_listener = {
    .enter = ti_enter,
    .leave = ti_leave,
    .preedit_string = ti_preedit,
    .commit_string = ti_commit_string,
    .delete_surrounding_text = ti_delete,
    .done = ti_done,
};

// ---- the input method ----
static int im_active, im_activations, im_surroundings, im_causes, im_dones;
static char im_surrounding_text[64];

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
    (void)d; (void)m; (void)c; (void)a;
    snprintf(im_surrounding_text, sizeof(im_surrounding_text), "%s", t ? t : "");
    im_surroundings++;
}
static void im_change_cause(void* d, struct zwp_input_method_v2* m, uint32_t c) {
    (void)d; (void)m;
    if (c == ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER)
        im_causes++;
}
static void im_content_type(void* d, struct zwp_input_method_v2* m, uint32_t h, uint32_t p) { (void)d; (void)m; (void)h; (void)p; }
static void im_done(void* d, struct zwp_input_method_v2* m) {
    (void)d; (void)m;
    im_dones++;
}
static void im_unavailable(void* d, struct zwp_input_method_v2* m) {
    (void)d; (void)m;
    fprintf(stderr, "input method unavailable\n");
    exit(1);
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

static void popup_rect(void* d, struct zwp_input_popup_surface_v2* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static const struct zwp_input_popup_surface_v2_listener popup_listener = {popup_rect};

static int grab_keymaps, grab_repeats;

static void grab_keymap(void* d, struct zwp_input_method_keyboard_grab_v2* g, uint32_t f, int32_t fd, uint32_t size) {
    (void)d; (void)g; (void)f; (void)size;
    close(fd);
    grab_keymaps++;
}
static void grab_key(void* d, struct zwp_input_method_keyboard_grab_v2* g, uint32_t s, uint32_t t, uint32_t k, uint32_t st) {
    (void)d; (void)g; (void)s; (void)t; (void)k; (void)st;
}
static void grab_mods(void* d, struct zwp_input_method_keyboard_grab_v2* g, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t e) {
    (void)d; (void)g; (void)s; (void)a; (void)b; (void)c; (void)e;
}
static void grab_repeat(void* d, struct zwp_input_method_keyboard_grab_v2* g, int32_t rate, int32_t delay) {
    (void)d; (void)g; (void)rate; (void)delay;
    grab_repeats++;
}
static const struct zwp_input_method_keyboard_grab_v2_listener grab_listener = {
    grab_keymap, grab_key, grab_mods, grab_repeat,
};

// the v3 keyboard: repeat_info does not exist there
static int old_keymaps;

static void kb_keymap(void* d, struct wl_keyboard* k, uint32_t f, int32_t fd, uint32_t size) {
    (void)d; (void)k; (void)f; (void)size;
    close(fd);
    old_keymaps++;
}
static void kb_enter(void* d, struct wl_keyboard* k, uint32_t s, struct wl_surface* su, struct wl_array* keys) {
    (void)d; (void)k; (void)s; (void)su; (void)keys;
}
static void kb_leave(void* d, struct wl_keyboard* k, uint32_t s, struct wl_surface* su) { (void)d; (void)k; (void)s; (void)su; }
static void kb_key(void* d, struct wl_keyboard* k, uint32_t s, uint32_t t, uint32_t key, uint32_t st) {
    (void)d; (void)k; (void)s; (void)t; (void)key; (void)st;
}
static void kb_mods(void* d, struct wl_keyboard* k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t e) {
    (void)d; (void)k; (void)s; (void)a; (void)b; (void)c; (void)e;
}
static const struct wl_keyboard_listener old_keyboard_listener = {kb_keymap, kb_enter, kb_leave, kb_key, kb_mods, NULL};

static struct wl_toplevel_ctx top;

static void pump(void) {
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
}

static int fail(const char* what) {
    fprintf(stderr, "%s\n", what);
    return 1;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!ti_mgr || !im_mgr || !seat3) return 2;

    struct zwp_input_method_v2* im = zwp_input_method_manager_v2_get_input_method(im_mgr, wl_seat_g);

    zwp_input_method_v2_add_listener(im, &im_listener, NULL);
    wl_keyboard_add_listener(wl_seat_get_keyboard(seat3), &old_keyboard_listener, NULL);

    wl_make_toplevel(&top, "text-input-focus", 200, 200, 0xff204060u);

    struct zwp_text_input_v3* ti = zwp_text_input_manager_v3_get_text_input(ti_mgr, wl_seat_g);

    zwp_text_input_v3_add_listener(ti, &ti_listener, NULL);
    pump();

    if (!ti_entered) return fail("the text input never entered");

    // surrounding text in the enabling commit comes with the activation
    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_surrounding_text(ti, "abc", 1, 1);
    zwp_text_input_v3_commit(ti);

    while (!(im_active && im_surroundings) && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (strcmp(im_surrounding_text, "abc")) return fail("the activation came without the surrounding text");

    // a change the IME did not make is announced as such
    zwp_text_input_v3_set_surrounding_text(ti, "abcd", 2, 2);
    zwp_text_input_v3_set_text_change_cause(ti, ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
    zwp_text_input_v3_commit(ti);

    while (!im_causes && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("activation forwarded\n");

    // the IME's popup, shown while input is active
    struct wl_surface* psurf = wl_compositor_create_surface(wl_comp);
    struct zwp_input_popup_surface_v2* popup = zwp_input_method_v2_get_input_popup_surface(im, psurf);

    zwp_input_popup_surface_v2_add_listener(popup, &popup_listener, NULL);
    wl_surface_attach(psurf, wl_solid(40, 20, 0xffe0e0e0u), 0, 0);
    wl_surface_damage(psurf, 0, 0, 40, 20);
    wl_surface_commit(psurf);

    // empty strings from the IME arrive as null
    zwp_input_method_v2_set_preedit_string(im, "", 0, 0);
    zwp_input_method_v2_commit_string(im, "");
    zwp_input_method_v2_commit(im, (uint32_t)im_dones);

    while (!(ti_null_preedits && ti_null_commits) && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("empty strings forwarded\n");

    // the scenario waits for the popup to show, then lets us go on
    while (access("go-disable", F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }

    // disabled: deactivated, popup gone; an IME commit then has no target
    zwp_text_input_v3_disable(ti);
    zwp_text_input_v3_commit(ti);

    while (im_active && wl_display_dispatch(wl_dpy) != -1) {
    }

    int commits = ti_commits;

    zwp_input_method_v2_commit_string(im, "lost");
    zwp_input_method_v2_commit(im, (uint32_t)im_dones);
    pump();

    if (ti_commits != commits) return fail("an IME commit reached a disabled text input");

    printf("disabled\n");

    // the scenario sees the popup hidden before input comes back
    while (access("go-refocus", F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }

    // enabled again, then another window takes the keyboard
    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_commit(ti);

    while (!im_active && wl_display_dispatch(wl_dpy) != -1) {
    }

    // a second window takes the keyboard: the text input leaves the first
    struct wl_toplevel_ctx second;

    wl_make_toplevel(&second, "text-input-focus-second", 200, 200, 0xff602040u);

    for (int i = 0; i < 300 && (im_active || !ti_leaves); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }

    if (im_active || !ti_leaves) {
        fprintf(stderr, "focus moved: im_active=%d ti_leaves=%d\n", im_active, ti_leaves);
        return 1;
    }

    printf("focus moved\n");

    // back on our window, grabbed by the IME: settings changes reach the grab
    struct zwp_input_method_keyboard_grab_v2* grab = zwp_input_method_v2_grab_keyboard(im);

    zwp_input_method_keyboard_grab_v2_add_listener(grab, &grab_listener, NULL);
    pump();

    int repeats = grab_repeats, keymaps = grab_keymaps, old = old_keymaps;

    printf("grabbed\n");

    while (!(grab_repeats > repeats && grab_keymaps > keymaps && old_keymaps > old) &&
           wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("text input focus done\n");

    return 0;
}
