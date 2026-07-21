// Feature: color-management-v1. The manager advertises its capabilities, and a
// surface with a PQ + BT.2020 image description is actually converted by the
// compositor — so the composited pixels differ from the raw bytes. The client
// drives two states (raw, then color-managed); the scenario compares them.

#include "wl_util.h"
#include <color-management-v1-client-protocol.h>

#ifndef COLOR_PIXEL
#define COLOR_PIXEL 0xFFB4783C
#endif

#ifndef COLOR_TRANSFER
#define COLOR_TRANSFER WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ
#endif

#ifndef COLOR_PRIMARIES
#define COLOR_PRIMARIES WP_COLOR_MANAGER_V1_PRIMARIES_BT2020
#endif

static struct wp_color_manager_v1* cm;
static struct wl_toplevel_ctx top;
static int got_intent, got_feature, cm_done;
static int desc_ready;

static void cm_intent(void* d, struct wp_color_manager_v1* m, uint32_t i) { (void)d; (void)m; (void)i; got_intent++; }
static void cm_feature(void* d, struct wp_color_manager_v1* m, uint32_t f) { (void)d; (void)m; (void)f; got_feature++; }
static void cm_tf(void* d, struct wp_color_manager_v1* m, uint32_t t) { (void)d; (void)m; (void)t; }
static void cm_prim(void* d, struct wp_color_manager_v1* m, uint32_t p) { (void)d; (void)m; (void)p; }
static void cm_done_ev(void* d, struct wp_color_manager_v1* m) { (void)d; (void)m; cm_done = 1; }
static const struct wp_color_manager_v1_listener cm_listener = {
    cm_intent, cm_feature, cm_tf, cm_prim, cm_done_ev,
};

static void desc_ready_ev(void* d, struct wp_image_description_v1* z, uint32_t id) {
    (void)d; (void)z; (void)id; desc_ready = 1;
}
static void desc_failed_ev(void* d, struct wp_image_description_v1* z, uint32_t c, const char* m) {
    (void)d; (void)z; (void)c; (void)m;
}
static const struct wp_image_description_v1_listener desc_listener = {desc_failed_ev, desc_ready_ev};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        cm = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

static void pump(int ms) {
    for (int i = 0; i < ms / 20; i++) { wl_display_roundtrip(wl_dpy); usleep(20000); }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!cm) { fprintf(stderr, "no color-manager\n"); return 1; }
    wp_color_manager_v1_add_listener(cm, &cm_listener, NULL);

    for (int i = 0; i < 100 && !cm_done; i++) { wl_display_roundtrip(wl_dpy); usleep(20000); }
    if (!cm_done || !got_intent || !got_feature) {
        fprintf(stderr, "handshake incomplete: intents=%d features=%d done=%d\n", got_intent, got_feature, cm_done);
        return 1;
    }
    printf("client_feat_color_mgmt: handshake ok (intents=%d features=%d)\n", got_intent, got_feature);

    // a mixed opaque color so both the PQ transfer and the BT.2020 gamut shift
    // are visible after conversion
    wl_make_toplevel(&top, "client_feat_color_mgmt", 300, 200, COLOR_PIXEL);
    printf("client_feat_color_mgmt: raw\n");
    pump(1000);

    // build the requested image description and attach it to the surface
    struct wp_image_description_creator_params_v1* params = wp_color_manager_v1_create_parametric_creator(cm);
    wp_image_description_creator_params_v1_set_tf_named(params, COLOR_TRANSFER);
    wp_image_description_creator_params_v1_set_primaries_named(params, COLOR_PRIMARIES);
#ifdef COLOR_MAX_CLL
    wp_image_description_creator_params_v1_set_max_cll(params, COLOR_MAX_CLL);
#endif
#ifdef COLOR_MAX_FALL
    wp_image_description_creator_params_v1_set_max_fall(params, COLOR_MAX_FALL);
#endif
    struct wp_image_description_v1* desc = wp_image_description_creator_params_v1_create(params);
    wp_image_description_v1_add_listener(desc, &desc_listener, NULL);
    for (int i = 0; i < 100 && !desc_ready; i++) { wl_display_roundtrip(wl_dpy); usleep(20000); }
    if (!desc_ready) { fprintf(stderr, "image description not ready\n"); return 1; }

    struct wp_color_management_surface_v1* cms = wp_color_manager_v1_get_surface(cm, top.surface);
    wp_color_management_surface_v1_set_image_description(cms, desc, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
    wl_surface_commit(top.surface);
    printf("client_feat_color_mgmt: managed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
