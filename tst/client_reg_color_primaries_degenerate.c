// Degenerate custom primaries (zero y coordinates, collapsed triangle) make
// the primaries matrix math divide by zero. Such an image description must
// answer failed(unsupported), not ready: a ready description would feed
// NaN/Inf into the scene shader.

#include "wl_util.h"
#include <color-management-v1-client-protocol.h>

static struct wp_color_manager_v1* color_mgr;
static int desc_ready, desc_failed;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        color_mgr = wl_registry_bind(r, name, &wp_color_manager_v1_interface,
                                     v < 3 ? v : 3);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void desc_ready_ev(void* d, struct wp_image_description_v1* i, uint32_t identity) {
    (void)d;
    (void)i;
    (void)identity;
    desc_ready = 1;
}
static void desc_ready2_ev(void* d, struct wp_image_description_v1* i,
                           uint32_t hi, uint32_t lo) {
    (void)d;
    (void)i;
    (void)hi;
    (void)lo;
    desc_ready = 1;
}
static void desc_failed_ev(void* d, struct wp_image_description_v1* i,
                           uint32_t cause, const char* msg) {
    (void)d;
    (void)i;
    (void)msg;
    if (cause == WP_IMAGE_DESCRIPTION_V1_CAUSE_UNSUPPORTED) desc_failed = 1;
}
static const struct wp_image_description_v1_listener desc_listener = {
    .failed = desc_failed_ev,
    .ready = desc_ready_ev,
    .ready2 = desc_ready2_ev,
};

static int probe(int32_t rx, int32_t ry, int32_t gx, int32_t gy,
                 int32_t bx, int32_t by, int32_t wx, int32_t wy,
                 const char* label) {
    desc_ready = desc_failed = 0;

    struct wp_image_description_creator_params_v1* creator =
        wp_color_manager_v1_create_parametric_creator(color_mgr);

    wp_image_description_creator_params_v1_set_tf_named(
        creator, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    wp_image_description_creator_params_v1_set_primaries(
        creator, rx, ry, gx, gy, bx, by, wx, wy);

    struct wp_image_description_v1* desc =
        wp_image_description_creator_params_v1_create(creator);

    wp_image_description_v1_add_listener(desc, &desc_listener, NULL);

    while (!desc_ready && !desc_failed && wl_display_dispatch(wl_dpy) != -1) {
    }

    wp_image_description_v1_destroy(desc);

    if (!desc_failed) {
        fprintf(stderr, "%s primaries produced a ready description\n", label);
        return 1;
    }

    printf("client_reg_color_primaries_degenerate: %s rejected\n", label);
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!color_mgr) {
        fprintf(stderr, "no color manager\n");
        return 1;
    }

    // zero y on a primary: division by zero in the primaries matrix
    if (probe(640000, 0, 300000, 600000, 150000, 60000, 312700, 329000, "zero-y"))
        return 1;

    // zero white point y: division by zero in the white scale
    if (probe(640000, 330000, 300000, 600000, 150000, 60000, 312700, 0, "zero-white"))
        return 1;

    // all primaries collapsed into one point: degenerate triangle
    if (probe(312700, 329000, 312700, 329000, 312700, 329000, 312700, 329000,
              "collapsed"))
        return 1;

    printf("client_reg_color_primaries_degenerate: ok\n");
    return 0;
}
