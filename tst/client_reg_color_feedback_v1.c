#include "wl_util.h"

#include <color-management-v1-client-protocol.h>

// A colour-management v1 client across an output colour change: its
// surface feedback hears the deprecated preferred_changed (not the v2
// preferred_changed2), a feedback whose surface is gone hears nothing, and
// a v1 wl_output (which has no done event) is left alone. With "info" it
// asks for the output's description instead: on an SDR output a v1 client
// is told the sRGB transfer function by the name v1 knows, not v2's
// compound power 2.4.

static struct wp_color_manager_v1* cm;
static struct wl_output* output;

static const struct wl_output_listener output_listener;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        cm = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && !output) {
        output = wl_registry_bind(r, name, &wl_output_interface, 1);
        wl_output_add_listener(output, &output_listener, NULL);
    }
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

static int desc_ready, desc_failed;
static int tf_named_count;
static uint32_t tf_named;

static void desc_failed_cb(void* d, struct wp_image_description_v1* desc, uint32_t cause, const char* msg) {
    (void)d; (void)desc; (void)cause; (void)msg;
    desc_failed = 1;
}
static void desc_ready_cb(void* d, struct wp_image_description_v1* desc, uint32_t identity) {
    (void)d; (void)desc; (void)identity;
    desc_ready = 1;
}
static const struct wp_image_description_v1_listener desc_listener = {
    .failed = desc_failed_cb,
    .ready = desc_ready_cb,
};

static void info_done(void* d, struct wp_image_description_info_v1* i) { (void)d; (void)i; }
static void info_icc(void* d, struct wp_image_description_info_v1* i, int32_t fd, uint32_t size) {
    (void)d; (void)i; (void)size;
    close(fd);
}
static void info_primaries(void* d, struct wp_image_description_info_v1* i, int32_t rx, int32_t ry, int32_t gx, int32_t gy,
                           int32_t bx, int32_t by, int32_t wx, int32_t wy) {
    (void)d; (void)i; (void)rx; (void)ry; (void)gx; (void)gy; (void)bx; (void)by; (void)wx; (void)wy;
}
static void info_primaries_named(void* d, struct wp_image_description_info_v1* i, uint32_t p) { (void)d; (void)i; (void)p; }
static void info_tf_power(void* d, struct wp_image_description_info_v1* i, uint32_t e) { (void)d; (void)i; (void)e; }
static void info_tf_named(void* d, struct wp_image_description_info_v1* i, uint32_t tf) {
    (void)d; (void)i;
    tf_named = tf;
    tf_named_count++;
}
static void info_luminances(void* d, struct wp_image_description_info_v1* i, uint32_t a, uint32_t b, uint32_t c) {
    (void)d; (void)i; (void)a; (void)b; (void)c;
}
static void info_target_primaries(void* d, struct wp_image_description_info_v1* i, int32_t rx, int32_t ry, int32_t gx,
                                  int32_t gy, int32_t bx, int32_t by, int32_t wx, int32_t wy) {
    (void)d; (void)i; (void)rx; (void)ry; (void)gx; (void)gy; (void)bx; (void)by; (void)wx; (void)wy;
}
static void info_target_luminance(void* d, struct wp_image_description_info_v1* i, uint32_t a, uint32_t b) {
    (void)d; (void)i; (void)a; (void)b;
}
static void info_target_max_cll(void* d, struct wp_image_description_info_v1* i, uint32_t v) { (void)d; (void)i; (void)v; }
static void info_target_max_fall(void* d, struct wp_image_description_info_v1* i, uint32_t v) { (void)d; (void)i; (void)v; }
static const struct wp_image_description_info_v1_listener info_listener = {
    .done = info_done,
    .icc_file = info_icc,
    .primaries = info_primaries,
    .primaries_named = info_primaries_named,
    .tf_power = info_tf_power,
    .tf_named = info_tf_named,
    .luminances = info_luminances,
    .target_primaries = info_target_primaries,
    .target_luminance = info_target_luminance,
    .target_max_cll = info_target_max_cll,
    .target_max_fall = info_target_max_fall,
};

static int run_info(void) {
    struct wp_color_management_output_v1* cm_output = wp_color_manager_v1_get_output(cm, output);
    struct wp_image_description_v1* desc = wp_color_management_output_v1_get_image_description(cm_output);

    wp_image_description_v1_add_listener(desc, &desc_listener, NULL);

    for (int i = 0; i < 50 && !desc_ready && !desc_failed; i++) {
        wl_display_roundtrip(wl_dpy);
    }

    if (!desc_ready) {
        fprintf(stderr, "the output's description never came ready\n");
        return 1;
    }

    wp_image_description_info_v1_add_listener(wp_image_description_v1_get_information(desc), &info_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (tf_named_count != 1 || tf_named != WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB) {
        fprintf(stderr, "a v1 client was told transfer function %u (%d times), not srgb\n", tf_named, tf_named_count);
        return 1;
    }

    printf("info v1 done\n");

    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!cm || !output) return 2;

    if (argc > 1 && !strcmp(argv[1], "info")) {
        return run_info();
    }

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
