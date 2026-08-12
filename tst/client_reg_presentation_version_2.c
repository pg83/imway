// wp_presentation v2: the compositor must advertise version >= 2. On the
// headless path there is no hardware vblank timestamp, so the presented
// feedback must not claim the VSYNC kind.

#include "wl_util.h"
#include <presentation-time-client-protocol.h>

static struct wp_presentation* presentation;
static uint32_t presentation_version;
static int got_clock, presented, present_flags = -1;
static struct wl_surface* surface;
static int committed;

static void pres_clock(void* d, struct wp_presentation* p, uint32_t clk) {
    (void)d; (void)p; (void)clk; got_clock = 1;
}
static const struct wp_presentation_listener pres_listener = {pres_clock};

static void fb_sync_output(void* d, struct wp_presentation_feedback* f, struct wl_output* o) {
    (void)d; (void)f; (void)o;
}
static void fb_presented(void* d, struct wp_presentation_feedback* f, uint32_t th, uint32_t tl,
                         uint32_t tn, uint32_t refresh, uint32_t sh, uint32_t sl, uint32_t flags) {
    (void)d; (void)f; (void)th; (void)tl; (void)tn; (void)refresh; (void)sh; (void)sl;
    presented = 1; present_flags = (int)flags;
}
static void fb_discarded(void* d, struct wp_presentation_feedback* f) { (void)d; (void)f; }
static const struct wp_presentation_feedback_listener fb_listener = {
    fb_sync_output, fb_presented, fb_discarded,
};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, wp_presentation_interface.name)) {
        presentation_version = v;
        presentation = wl_registry_bind(r, name, &wp_presentation_interface, v < 2 ? v : 2);
        wp_presentation_add_listener(presentation, &pres_listener, NULL);
    }
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    struct wp_presentation_feedback* fb = wp_presentation_feedback(presentation, surface);
    wp_presentation_feedback_add_listener(fb, &fb_listener, NULL);
    wl_surface_attach(surface, wl_solid(200, 150, 0xFF4080C0u), 0, 0);
    wl_surface_damage(surface, 0, 0, 200, 150);
    wl_surface_commit(surface);
    committed = 1;
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!presentation) {
        fprintf(stderr, "no wp_presentation\n");
        return 1;
    }
    if (presentation_version < 2) {
        fprintf(stderr, "wp_presentation at version %u, want >= 2\n", presentation_version);
        return 1;
    }

    surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &wl_tl_listener, NULL);
    xdg_toplevel_set_title(tl, "present2");
    xdg_toplevel_set_app_id(tl, "present2");
    wl_surface_commit(surface);

    for (int i = 0; i < 200 && !presented; i++) {
        if (wl_display_dispatch(wl_dpy) < 0) break;
    }

    if (!presented) {
        fprintf(stderr, "frame was never presented\n");
        return 1;
    }
    if (present_flags & WP_PRESENTATION_FEEDBACK_KIND_VSYNC) {
        fprintf(stderr, "software timestamp wrongly claims VSYNC (flags=%d)\n", present_flags);
        return 1;
    }

    printf("client_reg_presentation_version_2: presented, no false vsync\n");
    return 0;
}
