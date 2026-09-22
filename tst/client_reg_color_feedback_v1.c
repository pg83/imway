#include "wl_util.h"

#include <color-management-v1-client-protocol.h>

// A colour-management v1 client across an output colour change: its
// surface feedback hears the deprecated preferred_changed (not the v2
// preferred_changed2), a feedback whose surface is gone hears nothing, and
// a v1 wl_output (which has no done event) is left alone.

static struct wp_color_manager_v1* cm;
static struct wl_output* output;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        cm = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(r, name, &wl_output_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

struct counts {
    int changed, changed2;
};

static void fb_changed(void* d, struct wp_color_management_surface_feedback_v1* f, uint32_t id) {
    (void)f; (void)id;
    ((struct counts*)d)->changed++;
}
static void fb_changed2(void* d, struct wp_color_management_surface_feedback_v1* f, uint32_t hi, uint32_t lo) {
    (void)f; (void)hi; (void)lo;
    ((struct counts*)d)->changed2++;
}
static const struct wp_color_management_surface_feedback_v1_listener fb_listener = {
    .preferred_changed = fb_changed,
    .preferred_changed2 = fb_changed2,
};

static void o_geometry(void* d, struct wl_output* o, int32_t x, int32_t y, int32_t pw, int32_t ph,
                       int32_t sp, const char* make, const char* model, int32_t t) {
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)make; (void)model; (void)t;
}
static void o_mode(void* d, struct wl_output* o, uint32_t f, int32_t w, int32_t h, int32_t r) {
    (void)d; (void)o; (void)f; (void)w; (void)h; (void)r;
}
static const struct wl_output_listener output_listener = {o_geometry, o_mode, NULL, NULL, NULL, NULL};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!cm || !output) return 2;

    wl_output_add_listener(output, &output_listener, NULL);

    struct counts live = {0, 0}, dead = {0, 0};
    struct wl_surface* doomed = wl_compositor_create_surface(wl_comp);

    wp_color_management_surface_feedback_v1_add_listener(
        wp_color_manager_v1_get_surface_feedback(cm, wl_compositor_create_surface(wl_comp)), &fb_listener, &live);
    wp_color_management_surface_feedback_v1_add_listener(
        wp_color_manager_v1_get_surface_feedback(cm, doomed), &fb_listener, &dead);
    wl_surface_destroy(doomed);
    wl_display_roundtrip(wl_dpy);
    printf("waiting for change\n");

    for (int i = 0; i < 300 && !live.changed; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }

    wl_display_roundtrip(wl_dpy);
    printf("live %d/%d dead %d/%d\n", live.changed, live.changed2, dead.changed, dead.changed2);

    if (live.changed != 1 || live.changed2 || dead.changed || dead.changed2) {
        fprintf(stderr, "the v1 feedbacks heard the wrong change events\n");
        return 1;
    }

    printf("feedback v1 done\n");

    return 0;
}
