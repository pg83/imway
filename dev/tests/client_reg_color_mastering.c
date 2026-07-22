#include "wl_util.h"

#include <color-management-v1-client-protocol.h>

// #G-15: color-management completeness. The manager must advertise
// set_tf_power and set_mastering_display_primaries/luminance and the extra
// named primaries; a params image description exercising all of them must
// become ready without a protocol error.

static struct wp_color_manager_v1* cm;

static int feat_tf_power, feat_mastering_prim, feat_mastering_lum;
static int prim_dci, prim_adobe, prim_ntsc;

static void cm_intent(void* d, struct wp_color_manager_v1* m, uint32_t i) {
    (void)d; (void)m; (void)i;
}
static void cm_feature(void* d, struct wp_color_manager_v1* m, uint32_t f) {
    (void)d; (void)m;
    if (f == WP_COLOR_MANAGER_V1_FEATURE_SET_TF_POWER) feat_tf_power = 1;
    else if (f == WP_COLOR_MANAGER_V1_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES) feat_mastering_prim = 1;
    else if (f == WP_COLOR_MANAGER_V1_FEATURE_SET_LUMINANCES) feat_mastering_lum = 1;
}
static void cm_tf_named(void* d, struct wp_color_manager_v1* m, uint32_t t) {
    (void)d; (void)m; (void)t;
}
static void cm_primaries_named(void* d, struct wp_color_manager_v1* m, uint32_t p) {
    (void)d; (void)m;
    if (p == WP_COLOR_MANAGER_V1_PRIMARIES_DCI_P3) prim_dci = 1;
    else if (p == WP_COLOR_MANAGER_V1_PRIMARIES_ADOBE_RGB) prim_adobe = 1;
    else if (p == WP_COLOR_MANAGER_V1_PRIMARIES_NTSC) prim_ntsc = 1;
}
static int cm_done;
static void cm_done_cb(void* d, struct wp_color_manager_v1* m) {
    (void)d; (void)m;
    cm_done = 1;
}
static const struct wp_color_manager_v1_listener cm_listener = {
    .supported_intent = cm_intent,
    .supported_feature = cm_feature,
    .supported_tf_named = cm_tf_named,
    .supported_primaries_named = cm_primaries_named,
    .done = cm_done_cb,
};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        cm = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int desc_ready, desc_failed;

static void desc_ready_cb(void* d, struct wp_image_description_v1* i, uint32_t identity) {
    (void)d; (void)i; (void)identity;
    desc_ready = 1;
}
static void desc_failed_cb(void* d, struct wp_image_description_v1* i,
                           uint32_t cause, const char* msg) {
    (void)d; (void)i; (void)cause;
    fprintf(stderr, "image description failed: %s\n", msg ? msg : "?");
    desc_failed = 1;
}
static const struct wp_image_description_v1_listener desc_listener = {
    .failed = desc_failed_cb,
    .ready = desc_ready_cb,
};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!cm) return 2;

    wp_color_manager_v1_add_listener(cm, &cm_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!feat_tf_power || !feat_mastering_prim || !feat_mastering_lum) {
        fprintf(stderr, "missing features: tf_power=%d mastering_prim=%d lum=%d\n",
                feat_tf_power, feat_mastering_prim, feat_mastering_lum);
        return 1;
    }
    if (!prim_dci || !prim_adobe || !prim_ntsc) {
        fprintf(stderr, "missing named primaries: dci=%d adobe=%d ntsc=%d\n",
                prim_dci, prim_adobe, prim_ntsc);
        return 1;
    }

    struct wp_image_description_creator_params_v1* params =
        wp_color_manager_v1_create_parametric_creator(cm);
    wp_image_description_creator_params_v1_set_primaries_named(
        params, WP_COLOR_MANAGER_V1_PRIMARIES_DCI_P3);
    wp_image_description_creator_params_v1_set_tf_power(params, 2400000); // gamma 2.4
    wp_image_description_creator_params_v1_set_luminances(params, 1, 10000000, 2030000);
    wp_image_description_creator_params_v1_set_mastering_display_primaries(
        params, 680000, 320000, 265000, 690000, 150000, 60000, 31270, 32900);
    wp_image_description_creator_params_v1_set_mastering_luminance(params, 10, 10000000);
    wp_image_description_creator_params_v1_set_max_cll(params, 1000);
    wp_image_description_creator_params_v1_set_max_fall(params, 400);

    struct wp_image_description_v1* desc =
        wp_image_description_creator_params_v1_create(params);
    wp_image_description_v1_add_listener(desc, &desc_listener, NULL);

    while (!desc_ready && !desc_failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (desc_failed) return 1;

    printf("color-mastering done\n");
    fflush(stdout);
    return 0;
}
