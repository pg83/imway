// color-management-v1 protocol contract: current-version advertisement,
// complete output/preferred descriptions, PQ defaults, stable identities and
// change notifications.  Rendering itself is covered by the color/HDR tests.

#include "wl_util.h"
#include <color-management-v1-client-protocol.h>

static struct wp_color_manager_v1* color_mgr;
static struct wl_output* output;
static uint32_t color_version;
static int output_done;

static void output_geometry(void* d, struct wl_output* o, int32_t x, int32_t y,
                            int32_t pw, int32_t ph, int32_t subpixel,
                            const char* make, const char* model, int32_t transform) {
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)subpixel;
    (void)make; (void)model; (void)transform;
}
static void output_mode(void* d, struct wl_output* o, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh) {
    (void)d; (void)o; (void)flags; (void)width; (void)height; (void)refresh;
}
static void output_done_ev(void* d, struct wl_output* o) {
    (void)d; (void)o; output_done++;
}
static void output_scale(void* d, struct wl_output* o, int32_t scale) {
    (void)d; (void)o; (void)scale;
}
static void output_name(void* d, struct wl_output* o, const char* name) {
    (void)d; (void)o; (void)name;
}
static void output_description(void* d, struct wl_output* o, const char* desc) {
    (void)d; (void)o; (void)desc;
}
static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done_ev,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* interface, uint32_t version) {
    (void)d;
    if (!strcmp(interface, wp_color_manager_v1_interface.name)) {
        color_version = version;
        color_mgr = wl_registry_bind(registry, name, &wp_color_manager_v1_interface,
                                     version < 3 ? version : 3);
    } else if (!strcmp(interface, wl_output_interface.name)) {
        output = wl_registry_bind(registry, name, &wl_output_interface,
                                  version < 4 ? version : 4);
        wl_output_add_listener(output, &output_listener, NULL);
    }
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int got_intent;
static int got_parametric, got_luminances, got_mastering, got_windows_scrgb, got_other_feature;
static int got_srgb_tf, got_compound_tf, got_bt1886_tf, got_gamma22_tf;
static int got_linear_tf, got_pq_tf, got_hlg_tf, got_other_tf;
static int got_srgb_prim, got_bt2020_prim, got_other_prim;
static int manager_done;

static void manager_intent(void* d, struct wp_color_manager_v1* m, uint32_t value) {
    (void)d; (void)m;
    if (value == WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL) got_intent++;
}
static void manager_feature(void* d, struct wp_color_manager_v1* m, uint32_t value) {
    (void)d; (void)m;
    if (value == WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC) got_parametric++;
    else if (value == WP_COLOR_MANAGER_V1_FEATURE_SET_LUMINANCES) got_luminances++;
    else if (value == WP_COLOR_MANAGER_V1_FEATURE_SET_MASTERING_DISPLAY_PRIMARIES) got_mastering++;
    else if (value == WP_COLOR_MANAGER_V1_FEATURE_WINDOWS_SCRGB) got_windows_scrgb++;
    else got_other_feature++;
}
static void manager_tf(void* d, struct wp_color_manager_v1* m, uint32_t value) {
    (void)d; (void)m;
    if (value == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB) got_srgb_tf++;
    else if (value == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_COMPOUND_POWER_2_4) got_compound_tf++;
    else if (value == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886) got_bt1886_tf++;
    else if (value == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22) got_gamma22_tf++;
    else if (value == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR) got_linear_tf++;
    else if (value == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ) got_pq_tf++;
    else if (value == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG) got_hlg_tf++;
    else got_other_tf++;
}
static void manager_prim(void* d, struct wp_color_manager_v1* m, uint32_t value) {
    (void)d; (void)m;
    if (value == WP_COLOR_MANAGER_V1_PRIMARIES_SRGB) got_srgb_prim++;
    else if (value == WP_COLOR_MANAGER_V1_PRIMARIES_BT2020) got_bt2020_prim++;
    else got_other_prim++;
}
static void manager_done_ev(void* d, struct wp_color_manager_v1* m) {
    (void)d; (void)m; manager_done = 1;
}
static const struct wp_color_manager_v1_listener manager_listener = {
    .supported_intent = manager_intent,
    .supported_feature = manager_feature,
    .supported_tf_named = manager_tf,
    .supported_primaries_named = manager_prim,
    .done = manager_done_ev,
};

struct desc_state {
    int failed;
    int ready;
    int ready2;
    uint32_t cause;
    uint64_t identity;
};

static void desc_failed(void* d, struct wp_image_description_v1* desc,
                        uint32_t cause, const char* message) {
    (void)desc;
    struct desc_state* state = d;
    state->failed = 1;
    state->cause = cause;
    fprintf(stderr, "image description failed: %u %s\n", cause, message);
}
static void desc_ready(void* d, struct wp_image_description_v1* desc, uint32_t identity) {
    (void)desc;
    struct desc_state* state = d;
    state->ready++;
    state->identity = identity;
}
static void desc_ready2(void* d, struct wp_image_description_v1* desc,
                        uint32_t hi, uint32_t lo) {
    (void)desc;
    struct desc_state* state = d;
    state->ready2++;
    state->identity = ((uint64_t)hi << 32) | lo;
}
static const struct wp_image_description_v1_listener desc_listener = {
    .failed = desc_failed,
    .ready = desc_ready,
    .ready2 = desc_ready2,
};

struct info_state {
    int done;
    int primaries_xy;
    int primaries_named;
    int tf_named;
    int luminances;
    int target_primaries;
    int target_luminance;
    int target_max_cll;
    int target_max_fall;
    uint32_t primaries;
    uint32_t tf;
    uint32_t min_lum, max_lum, ref_lum;
    int32_t target[8];
    int32_t primary[8];
    uint32_t target_min_lum, target_max_lum;
};

static void info_done(void* d, struct wp_image_description_info_v1* info) {
    (void)info; ((struct info_state*)d)->done++;
}
static void info_icc(void* d, struct wp_image_description_info_v1* info,
                     int32_t fd, uint32_t size) {
    (void)d; (void)info; (void)size; close(fd);
}
static void info_primaries(void* d, struct wp_image_description_info_v1* info,
                           int32_t rx, int32_t ry, int32_t gx, int32_t gy,
                           int32_t bx, int32_t by, int32_t wx, int32_t wy) {
    (void)info;
    struct info_state* state = d;
    int32_t value[8] = {rx, ry, gx, gy, bx, by, wx, wy};
    memcpy(state->primary, value, sizeof(value));
    state->primaries_xy++;
}
static void info_primaries_named(void* d, struct wp_image_description_info_v1* info,
                                 uint32_t value) {
    (void)info;
    struct info_state* state = d;
    state->primaries_named++;
    state->primaries = value;
}
static void info_tf_power(void* d, struct wp_image_description_info_v1* info, uint32_t value) {
    (void)d; (void)info; (void)value;
}
static void info_tf_named(void* d, struct wp_image_description_info_v1* info, uint32_t value) {
    (void)info;
    struct info_state* state = d;
    state->tf_named++;
    state->tf = value;
}
static void info_luminances(void* d, struct wp_image_description_info_v1* info,
                            uint32_t min_lum, uint32_t max_lum, uint32_t ref_lum) {
    (void)info;
    struct info_state* state = d;
    state->luminances++;
    state->min_lum = min_lum;
    state->max_lum = max_lum;
    state->ref_lum = ref_lum;
}
static void info_target_primaries(void* d, struct wp_image_description_info_v1* info,
                                  int32_t rx, int32_t ry, int32_t gx, int32_t gy,
                                  int32_t bx, int32_t by, int32_t wx, int32_t wy) {
    (void)info;
    struct info_state* state = d;
    int32_t value[8] = {rx, ry, gx, gy, bx, by, wx, wy};
    memcpy(state->target, value, sizeof(value));
    state->target_primaries++;
}
static void info_target_luminance(void* d, struct wp_image_description_info_v1* info,
                                  uint32_t min_lum, uint32_t max_lum) {
    (void)info;
    struct info_state* state = d;
    state->target_luminance++;
    state->target_min_lum = min_lum;
    state->target_max_lum = max_lum;
}
static void info_target_max_cll(void* d, struct wp_image_description_info_v1* info,
                                uint32_t value) {
    (void)info; (void)value; ((struct info_state*)d)->target_max_cll++;
}
static void info_target_max_fall(void* d, struct wp_image_description_info_v1* info,
                                 uint32_t value) {
    (void)info; (void)value; ((struct info_state*)d)->target_max_fall++;
}
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

static int cm_output_changed;
static void cm_output_changed_ev(void* d, struct wp_color_management_output_v1* o) {
    (void)d; (void)o; cm_output_changed++;
}
static const struct wp_color_management_output_v1_listener cm_output_listener = {
    .image_description_changed = cm_output_changed_ev,
};

static int preferred_changed, preferred_changed2;
static uint64_t preferred_identity;
static void preferred_changed_ev(void* d, struct wp_color_management_surface_feedback_v1* f,
                                 uint32_t identity) {
    (void)d; (void)f; preferred_changed++;
    preferred_identity = identity;
}
static void preferred_changed2_ev(void* d, struct wp_color_management_surface_feedback_v1* f,
                                  uint32_t hi, uint32_t lo) {
    (void)d; (void)f; preferred_changed2++;
    preferred_identity = ((uint64_t)hi << 32) | lo;
}
static const struct wp_color_management_surface_feedback_v1_listener feedback_listener = {
    .preferred_changed = preferred_changed_ev,
    .preferred_changed2 = preferred_changed2_ev,
};

static int boot_color(void) {
    if (wl_boot()) return 1;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!color_mgr || !output) {
        fprintf(stderr, "missing color/output globals\n");
        return 1;
    }
    wp_color_manager_v1_add_listener(color_mgr, &manager_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    return 0;
}

static int check_manager(void) {
    if (color_version != 3 || !manager_done || got_intent != 1 ||
        got_parametric != 1 || got_luminances != 1 || got_mastering ||
        got_windows_scrgb != 1 || got_other_feature ||
        got_srgb_tf || got_compound_tf != 1 || got_bt1886_tf != 1 ||
        got_gamma22_tf != 1 || got_linear_tf != 1 || got_pq_tf != 1 ||
        got_hlg_tf != 1 || got_other_tf ||
        got_srgb_prim != 1 || got_bt2020_prim != 1 || got_other_prim) {
        fprintf(stderr, "bad manager: v=%u done=%d intent=%d features=%d/%d/%d/%d/%d "
                "tf=%d/%d/%d/%d/%d/%d/%d/%d prim=%d/%d/%d\n", color_version, manager_done,
                got_intent, got_parametric, got_luminances, got_mastering,
                got_windows_scrgb, got_other_feature,
                got_srgb_tf, got_compound_tf, got_bt1886_tf, got_gamma22_tf,
                got_linear_tf, got_pq_tf, got_hlg_tf, got_other_tf,
                got_srgb_prim, got_bt2020_prim, got_other_prim);
        return 1;
    }
    return 0;
}

static int wait_desc(struct desc_state* state) {
    for (int i = 0; i < 20 && !state->failed && !state->ready && !state->ready2; i++)
        wl_display_roundtrip(wl_dpy);
    if (state->failed || state->ready || state->ready2 != 1 || !state->identity) {
        fprintf(stderr, "bad ready events: failed=%d ready=%d ready2=%d id=%llu\n",
                state->failed, state->ready, state->ready2,
                (unsigned long long)state->identity);
        return 1;
    }
    return 0;
}

static int run_info(int hdr, uint32_t target_min, uint32_t target_max) {
    struct wp_color_management_output_v1* cm_output =
        wp_color_manager_v1_get_output(color_mgr, output);
    wp_color_management_output_v1_add_listener(cm_output, &cm_output_listener, NULL);

    struct desc_state output_state = {0};
    struct wp_image_description_v1* desc =
        wp_color_management_output_v1_get_image_description(cm_output);
    wp_image_description_v1_add_listener(desc, &desc_listener, &output_state);
    if (wait_desc(&output_state)) return 1;

    struct info_state info = {0};
    struct wp_image_description_info_v1* information =
        wp_image_description_v1_get_information(desc);
    wp_image_description_info_v1_add_listener(information, &info_listener, &info);
    wl_display_roundtrip(wl_dpy);

    static const int32_t srgb[8] = {640000, 330000, 300000, 600000,
                                    150000, 60000, 312700, 329000};
    static const int32_t bt2020[8] = {708000, 292000, 170000, 797000,
                                      131000, 46000, 312700, 329000};
    const int32_t* target = hdr ? bt2020 : srgb;
    uint32_t want_prim = hdr ? WP_COLOR_MANAGER_V1_PRIMARIES_BT2020 :
                               WP_COLOR_MANAGER_V1_PRIMARIES_SRGB;
    uint32_t want_tf = hdr ? WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ :
                             WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_COMPOUND_POWER_2_4;
    uint32_t want_min = hdr ? 50 : 2000;
    uint32_t want_max = hdr ? 10000 : 80;
    uint32_t want_ref = hdr ? 203 : 80;
    uint32_t want_target_min = target_min ? target_min : hdr ? 1 : want_min;
    uint32_t want_target_max = target_max ? target_max : hdr ? 1000 : want_max;

    if (info.done != 1 || info.primaries_xy != 1 || info.primaries_named != 1 || info.tf_named != 1 ||
        info.luminances != 1 || info.target_primaries != 1 || info.target_luminance != 1 ||
        info.target_max_cll || info.target_max_fall || info.primaries != want_prim ||
        info.tf != want_tf || info.min_lum != want_min || info.max_lum != want_max ||
        info.ref_lum != want_ref || memcmp(info.primary, target, sizeof(info.primary)) ||
        memcmp(info.target, target, sizeof(info.target)) ||
        info.target_min_lum != want_target_min ||
        info.target_max_lum != want_target_max) {
        fprintf(stderr, "incomplete/wrong info: done=%d prim=%d:%u tf=%d:%u "
                "lum=%d:%u/%u/%u target=%d/%d:%u/%u cll=%d fall=%d\n",
                info.done, info.primaries_named, info.primaries, info.tf_named, info.tf,
                info.luminances, info.min_lum, info.max_lum, info.ref_lum,
                info.target_primaries, info.target_luminance,
                info.target_min_lum, info.target_max_lum,
                info.target_max_cll, info.target_max_fall);
        return 1;
    }

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_color_management_surface_feedback_v1* feedback =
        wp_color_manager_v1_get_surface_feedback(color_mgr, surface);
    wp_color_management_surface_feedback_v1_add_listener(feedback, &feedback_listener, NULL);
    struct desc_state preferred_state = {0};
    struct wp_image_description_v1* preferred =
        wp_color_management_surface_feedback_v1_get_preferred_parametric(feedback);
    wp_image_description_v1_add_listener(preferred, &desc_listener, &preferred_state);
    if (wait_desc(&preferred_state)) return 1;
    if (preferred_state.identity != output_state.identity) {
        fprintf(stderr, "output/preferred identities differ: %llu != %llu\n",
                (unsigned long long)output_state.identity,
                (unsigned long long)preferred_state.identity);
        return 1;
    }
    printf("color-protocol: info %s ok id=%llu\n", hdr ? "hdr" : "sdr",
           (unsigned long long)output_state.identity);
    return 0;
}

static int run_pq_defaults(void) {
    struct wp_image_description_creator_params_v1* params =
        wp_color_manager_v1_create_parametric_creator(color_mgr);
    wp_image_description_creator_params_v1_set_tf_named(
        params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    wp_image_description_creator_params_v1_set_primaries_named(
        params, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);
    wp_image_description_creator_params_v1_set_max_cll(params, 4000);
    wp_image_description_creator_params_v1_set_max_fall(params, 1000);
    struct desc_state state = {0};
    struct wp_image_description_v1* desc =
        wp_image_description_creator_params_v1_create(params);
    wp_image_description_v1_add_listener(desc, &desc_listener, &state);
    if (wait_desc(&state)) return 1;

    params = wp_color_manager_v1_create_parametric_creator(color_mgr);
    wp_image_description_creator_params_v1_set_luminances(params, 50, 0, 203);
    wp_image_description_creator_params_v1_set_primaries_named(
        params, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);
    wp_image_description_creator_params_v1_set_tf_named(
        params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    state = (struct desc_state){0};
    desc = wp_image_description_creator_params_v1_create(params);
    wp_image_description_v1_add_listener(desc, &desc_listener, &state);
    if (wait_desc(&state)) return 1;
    printf("color-protocol: PQ defaults ok\n");
    return 0;
}

static int run_windows_scrgb(void) {
    struct desc_state state = {0};
    struct wp_image_description_v1* desc =
        wp_color_manager_v1_create_windows_scrgb(color_mgr);
    wp_image_description_v1_add_listener(desc, &desc_listener, &state);
    if (wait_desc(&state)) return 1;
    wp_image_description_v1_get_information(desc);
    if (wl_expect_error(wp_image_description_v1_interface.name,
                        WP_IMAGE_DESCRIPTION_V1_ERROR_NO_INFORMATION)) {
        return 1;
    }
    printf("color-protocol: Windows-scRGB ready\n");
    return 0;
}

static int run_changes(void) {
    struct wp_color_management_output_v1* cm_output =
        wp_color_manager_v1_get_output(color_mgr, output);
    wp_color_management_output_v1_add_listener(cm_output, &cm_output_listener, NULL);
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_color_management_surface_feedback_v1* feedback =
        wp_color_manager_v1_get_surface_feedback(color_mgr, surface);
    wp_color_management_surface_feedback_v1_add_listener(feedback, &feedback_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    int initial_output_done = output_done;
    printf("color-protocol: waiting for change\n");
    for (int i = 0; i < 200 && (!cm_output_changed || !preferred_changed2); i++) {
        if (wl_display_dispatch(wl_dpy) < 0) return 1;
    }
    if (cm_output_changed != 1 || preferred_changed || preferred_changed2 != 1 ||
        !preferred_identity || output_done <= initial_output_done) {
        fprintf(stderr, "bad change events: output=%d old=%d new=%d id=%llu done=%d/%d\n",
                cm_output_changed, preferred_changed, preferred_changed2,
                (unsigned long long)preferred_identity, output_done, initial_output_done);
        return 1;
    }
    struct desc_state state = {0};
    struct wp_image_description_v1* desc =
        wp_color_management_surface_feedback_v1_get_preferred(feedback);
    wp_image_description_v1_add_listener(desc, &desc_listener, &state);
    if (wait_desc(&state) || state.identity != preferred_identity) {
        fprintf(stderr, "changed preferred identity was not stable\n");
        return 1;
    }
    printf("color-protocol: changes ok id=%llu\n",
           (unsigned long long)preferred_identity);
    return 0;
}

static int run_output_resource_lifetime(void) {
    struct wp_color_management_output_v1* cm_output =
        wp_color_manager_v1_get_output(color_mgr, output);
    wl_output_release(output);
    output = NULL;
    wl_display_roundtrip(wl_dpy);

    struct desc_state state = {0};
    struct wp_image_description_v1* desc =
        wp_color_management_output_v1_get_image_description(cm_output);
    wp_image_description_v1_add_listener(desc, &desc_listener, &state);
    wl_display_roundtrip(wl_dpy);
    if (wait_desc(&state)) {
        fprintf(stderr, "destroying wl_output incorrectly made extension inert\n");
        return 1;
    }
    printf("color-protocol: wl_output resource lifetime ok\n");
    return 0;
}

static int run_feedback_inert(void) {
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_color_management_surface_feedback_v1* feedback =
        wp_color_manager_v1_get_surface_feedback(color_mgr, surface);
    wl_surface_destroy(surface);
    wl_display_roundtrip(wl_dpy);
    wp_color_management_surface_feedback_v1_get_preferred(feedback);
    if (wl_expect_error(wp_color_management_surface_feedback_v1_interface.name,
                        WP_COLOR_MANAGEMENT_SURFACE_FEEDBACK_V1_ERROR_INERT)) {
        return 1;
    }
    printf("color-protocol: inert feedback ok\n");
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(15);
    if (argc < 2 || boot_color() || check_manager()) return 2;
    if (!strcmp(argv[1], "info-sdr") && argc == 2) return run_info(0, 0, 0);
    if (!strcmp(argv[1], "info-hdr") && argc == 2) return run_info(1, 0, 0);
    if (!strcmp(argv[1], "info-volume") && argc == 4)
        return run_info(1, (uint32_t)strtoul(argv[2], NULL, 10),
                        (uint32_t)strtoul(argv[3], NULL, 10));
    if (!strcmp(argv[1], "pq-defaults")) return run_pq_defaults();
    if (!strcmp(argv[1], "windows-scrgb")) return run_windows_scrgb();
    if (!strcmp(argv[1], "changes")) return run_changes();
    if (!strcmp(argv[1], "output-resource-lifetime")) return run_output_resource_lifetime();
    if (!strcmp(argv[1], "feedback-inert")) return run_feedback_inert();
    return 2;
}
