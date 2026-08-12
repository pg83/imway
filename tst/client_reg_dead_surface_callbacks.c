// Regression: frame callbacks and presentation feedback on surfaces that
// die before the frame is shown. Neither a role-less surface nor a mapped
// toplevel destroyed with pending callbacks may take the compositor down,
// and the callback machinery must keep working for the next window.

#include "wl_util.h"
#include <presentation-time-client-protocol.h>

static struct wp_presentation* presentation;
static int frame_done;

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_presentation_interface.name))
        presentation = wl_registry_bind(r, name, &wp_presentation_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

static void cb_done(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d; (void)t;
    wl_callback_destroy(cb);
    frame_done = 1;
}
static const struct wl_callback_listener cb_listener = {cb_done};

static void fb_sync_output(void* d, struct wp_presentation_feedback* f, struct wl_output* o) {
    (void)d; (void)f; (void)o;
}
static void fb_presented(void* d, struct wp_presentation_feedback* f, uint32_t a, uint32_t b,
                         uint32_t c, uint32_t r, uint32_t e, uint32_t g, uint32_t fl) {
    (void)d; (void)a; (void)b; (void)c; (void)r; (void)e; (void)g; (void)fl;
    wp_presentation_feedback_destroy(f);
}
static void fb_discarded(void* d, struct wp_presentation_feedback* f) {
    (void)d;
    wp_presentation_feedback_destroy(f);
}
static const struct wp_presentation_feedback_listener fb_listener = {fb_sync_output, fb_presented,
                                                                     fb_discarded};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!presentation) { fprintf(stderr, "no wp_presentation\n"); return 1; }

    // phase 1: a role-less surface with pending callbacks, destroyed at once
    struct wl_surface* bare = wl_compositor_create_surface(wl_comp);
    wl_surface_attach(bare, wl_solid(50, 50, 0xFF808080), 0, 0);
    struct wl_callback* cb = wl_surface_frame(bare);
    wl_callback_add_listener(cb, &cb_listener, NULL);
    struct wp_presentation_feedback* fb = wp_presentation_feedback(presentation, bare);
    wp_presentation_feedback_add_listener(fb, &fb_listener, NULL);
    wl_surface_commit(bare);
    wl_surface_destroy(bare);
    wl_display_roundtrip(wl_dpy);
    printf("bare surface destroyed\n");

    // phase 2: a mapped toplevel destroyed with callbacks in flight
    struct wl_toplevel_ctx t1;
    wl_make_toplevel(&t1, "shortlived", 200, 150, 0xFF0000FF);
    cb = wl_surface_frame(t1.surface);
    wl_callback_add_listener(cb, &cb_listener, NULL);
    fb = wp_presentation_feedback(presentation, t1.surface);
    wp_presentation_feedback_add_listener(fb, &fb_listener, NULL);
    wl_surface_commit(t1.surface);
    xdg_toplevel_destroy(t1.tl);
    xdg_surface_destroy(t1.xs);
    wl_surface_destroy(t1.surface);
    wl_display_roundtrip(wl_dpy);
    printf("mapped toplevel destroyed\n");

    // phase 3: the machinery still works for a fresh window
    struct wl_toplevel_ctx t2;
    wl_make_toplevel(&t2, "survivor", 200, 150, 0xFFFF0000);
    frame_done = 0;
    cb = wl_surface_frame(t2.surface);
    wl_callback_add_listener(cb, &cb_listener, NULL);
    wl_surface_attach(t2.surface, wl_solid(200, 150, 0xFFFF0000), 0, 0);
    wl_surface_damage(t2.surface, 0, 0, 200, 150);
    wl_surface_commit(t2.surface);
    while (!frame_done && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("callbacks alive\n");
    return 0;
}
