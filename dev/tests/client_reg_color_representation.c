#include "wl_util.h"
#include <color-representation-v1-client-protocol.h>

static struct wp_color_representation_manager_v1* manager;
static int alpha_electrical, alpha_optical, alpha_straight;
static int identity_full, other_coefficients, done;

static void alpha(void* data, struct wp_color_representation_manager_v1* obj,
                  uint32_t mode) {
    (void)data; (void)obj;
    alpha_electrical += mode == WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_ELECTRICAL;
    alpha_optical += mode == WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_OPTICAL;
    alpha_straight += mode == WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT;
}

static void coefficients(void* data, struct wp_color_representation_manager_v1* obj,
                         uint32_t coeff, uint32_t range) {
    (void)data; (void)obj;
    if (coeff == WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY &&
        range == WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL) identity_full++;
    else other_coefficients++;
}

static void manager_done(void* data, struct wp_color_representation_manager_v1* obj) {
    (void)data; (void)obj; done++;
}

static const struct wp_color_representation_manager_v1_listener manager_listener = {
    .supported_alpha_mode = alpha,
    .supported_coefficients_and_ranges = coefficients,
    .done = manager_done,
};

static void extra_global(void* data, struct wl_registry* registry, uint32_t name,
                         const char* interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, wp_color_representation_manager_v1_interface.name))
        manager = wl_registry_bind(registry, name,
                                   &wp_color_representation_manager_v1_interface, 1);
}
static void extra_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void wait_gate(const char* name) {
    while (access(name, F_OK) && errno == ENOENT) usleep(10000);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!manager) return 1;
    wp_color_representation_manager_v1_add_listener(manager, &manager_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (alpha_electrical != 1 || alpha_optical != 1 || alpha_straight != 1 ||
        identity_full != 1 || other_coefficients || done != 1) return 1;

    struct wl_toplevel_ctx top = {};
    wl_make_toplevel(&top, "color-representation", 300, 200, 0xff204080);
    wl_surface_attach(top.surface, wl_solid(300, 200, 0x80c80000), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 300, 200);
    wl_surface_commit(top.surface);
    puts("client_reg_color_representation: electrical");
    wl_display_flush(wl_dpy);
    wait_gate("repr-next-1");

    struct wp_color_representation_surface_v1* repr =
        wp_color_representation_manager_v1_get_surface(manager, top.surface);
    wp_color_representation_surface_v1_set_alpha_mode(
        repr, WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT);
    wl_surface_commit(top.surface);
    puts("client_reg_color_representation: straight");
    wl_display_flush(wl_dpy);
    wait_gate("repr-next-2");

    // sRGB(147) decodes to roughly half the optical intensity of sRGB(200):
    // this buffer is premultiplied after EOTF, unlike the first one.
    wl_surface_attach(top.surface, wl_solid(300, 200, 0x80930000), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 300, 200);
    wp_color_representation_surface_v1_set_alpha_mode(
        repr, WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_OPTICAL);
    wl_surface_commit(top.surface);
    puts("client_reg_color_representation: optical");
    wl_display_flush(wl_dpy);
    wait_gate("repr-next-3");

    wp_color_representation_surface_v1_destroy(repr);
    wl_surface_commit(top.surface);
    puts("client_reg_color_representation: reset");
    wl_display_flush(wl_dpy);
    while (wl_display_dispatch(wl_dpy) != -1) {}
    return 0;
}
