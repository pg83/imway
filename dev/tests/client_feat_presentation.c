// Feature: presentation-time. After a commit the compositor must report the
// frame's fate through wp_presentation_feedback — a presented (or discarded)
// event, plus a clock_id at bind time.

#include "wl_util.h"
#include <presentation-time-client-protocol.h>

static struct wp_presentation* presentation;
static struct wl_toplevel_ctx top;
static int got_clock, presented, discarded;

static void pres_clock_id(void* d, struct wp_presentation* p, uint32_t id) {
    (void)d; (void)p; (void)id;
    got_clock = 1;
}
static const struct wp_presentation_listener pres_listener = {pres_clock_id};

static void fb_sync_output(void* d, struct wp_presentation_feedback* f, struct wl_output* o) {
    (void)d; (void)f; (void)o;
}
static void fb_presented(void* d, struct wp_presentation_feedback* f, uint32_t th, uint32_t tl,
                         uint32_t tn, uint32_t refresh, uint32_t sh, uint32_t sl, uint32_t flags) {
    (void)d; (void)f; (void)th; (void)tl; (void)tn; (void)refresh; (void)sh; (void)sl; (void)flags;
    presented = 1;
}
static void fb_discarded(void* d, struct wp_presentation_feedback* f) {
    (void)d; (void)f; discarded = 1;
}
static const struct wp_presentation_feedback_listener fb_listener = {
    fb_sync_output, fb_presented, fb_discarded,
};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_presentation_interface.name))
        presentation = wl_registry_bind(r, name, &wp_presentation_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!presentation) { fprintf(stderr, "no wp_presentation\n"); return 1; }
    wp_presentation_add_listener(presentation, &pres_listener, NULL);

    wl_make_toplevel(&top, "client_feat_presentation", 300, 200, 0xFFFF0000);

    // request feedback and drive commits until the frame is presented
    for (int i = 0; i < 100 && !presented; i++) {
        struct wp_presentation_feedback* fb = wp_presentation_feedback(presentation, top.surface);
        wp_presentation_feedback_add_listener(fb, &fb_listener, NULL);
        wl_surface_attach(top.surface, wl_solid(300, 200, 0xFFFF0000), 0, 0);
        wl_surface_damage(top.surface, 0, 0, 300, 200);
        wl_surface_commit(top.surface);
        wl_display_roundtrip(wl_dpy);
        usleep(30000);
    }

    if (!got_clock) { fprintf(stderr, "no clock_id\n"); return 1; }
    if (!presented) { fprintf(stderr, "no presented (discarded=%d)\n", discarded); return 1; }
    printf("client_feat_presentation: presented, clock_id ok\n");
    return 0;
}
