#include "wl_util.h"
#include <color-representation-v1-client-protocol.h>

static struct wp_color_representation_manager_v1* manager;
static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_color_representation_manager_v1_interface.name))
        manager = wl_registry_bind(r, name, &wp_color_representation_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t name) {
    (void)d; (void)r; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(int argc, char** argv) {
    alarm(10);
    if (argc != 2 || wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!manager) return 1;
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    struct wp_color_representation_surface_v1* repr =
        wp_color_representation_manager_v1_get_surface(manager, surface);

    if (!strcmp(argv[1], "duplicate")) {
        wp_color_representation_manager_v1_get_surface(manager, surface);
        return wl_expect_error(wp_color_representation_manager_v1_interface.name,
                               WP_COLOR_REPRESENTATION_MANAGER_V1_ERROR_SURFACE_EXISTS);
    }
    if (!strcmp(argv[1], "invalid-alpha")) {
        wp_color_representation_surface_v1_set_alpha_mode(repr, 99);
        return wl_expect_error(wp_color_representation_surface_v1_interface.name,
                               WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_ALPHA_MODE);
    }
    if (!strcmp(argv[1], "invalid-coefficients")) {
        wp_color_representation_surface_v1_set_coefficients_and_range(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_FCC,
            WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL);
        return wl_expect_error(wp_color_representation_surface_v1_interface.name,
                               WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_COEFFICIENTS);
    }
    if (!strcmp(argv[1], "invalid-range")) {
        wp_color_representation_surface_v1_set_coefficients_and_range(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY,
            WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED);
        return wl_expect_error(wp_color_representation_surface_v1_interface.name,
                               WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_COEFFICIENTS);
    }
    if (!strcmp(argv[1], "invalid-chroma")) {
        wp_color_representation_surface_v1_set_chroma_location(repr, 99);
        return wl_expect_error(wp_color_representation_surface_v1_interface.name,
                               WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_CHROMA_LOCATION);
    }
    if (!strcmp(argv[1], "chroma-rgb")) {
        wp_color_representation_surface_v1_set_chroma_location(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_0);
        wl_surface_attach(surface, wl_solid(16, 16, 0xffffffff), 0, 0);
        wl_surface_commit(surface);
        return wl_expect_error(wp_color_representation_surface_v1_interface.name,
                               WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_PIXEL_FORMAT);
    }
    if (!strcmp(argv[1], "coefficients-rgb")) {
        wp_color_representation_surface_v1_set_coefficients_and_range(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709,
            WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED);
        wl_surface_attach(surface, wl_solid(16, 16, 0xffffffff), 0, 0);
        wl_surface_commit(surface);
        return wl_expect_error(wp_color_representation_surface_v1_interface.name,
                               WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_PIXEL_FORMAT);
    }
    if (!strcmp(argv[1], "cached-rgb")) {
        // on a synchronized subsurface: the YCbCr matrix waits in the cache
        // with no buffer to object to, and still refuses the RGB buffer a
        // second cached commit brings
        struct wl_surface* parent = wl_compositor_create_surface(wl_comp);
        wl_subcompositor_get_subsurface(wl_subcomp, surface, parent);
        wp_color_representation_surface_v1_set_coefficients_and_range(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709,
            WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED);
        wl_surface_commit(surface);
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "a cached representation without a buffer was refused\n");
            return 1;
        }
        wl_surface_attach(surface, wl_solid(16, 16, 0xffffffff), 0, 0);
        wl_surface_commit(surface);
        return wl_expect_error(wp_color_representation_surface_v1_interface.name,
                               WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_PIXEL_FORMAT);
    }
    if (!strcmp(argv[1], "chroma-zero")) {
        // below the first defined location
        wp_color_representation_surface_v1_set_chroma_location(repr, 0);
        return wl_expect_error(wp_color_representation_surface_v1_interface.name,
                               WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_CHROMA_LOCATION);
    }
    if (!strcmp(argv[1], "stacked")) {
        // several settings before one commit build on each other, over
        // every YCbCr matrix and both ranges
        wp_color_representation_surface_v1_set_coefficients_and_range(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601,
            WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED);
        wp_color_representation_surface_v1_set_chroma_location(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_2);
        wp_color_representation_surface_v1_set_alpha_mode(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT);
        wp_color_representation_surface_v1_set_coefficients_and_range(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020,
            WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL);
        wl_surface_commit(surface);
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "stacked representation settings were refused\n");
            return 1;
        }
        return 0;
    }
    if (!strcmp(argv[1], "yuv-bad-range")) {
        // a YCbCr matrix still needs one of the two ranges
        wp_color_representation_surface_v1_set_coefficients_and_range(
            repr, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709, 99);
        return wl_expect_error(wp_color_representation_surface_v1_interface.name,
                               WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_COEFFICIENTS);
    }
    if (!strcmp(argv[1], "inert-destroy")) {
        // the wl_surface goes first: the object is inert, and destroying it
        // is still allowed
        wl_surface_destroy(surface);
        wl_display_roundtrip(wl_dpy);
        wp_color_representation_surface_v1_destroy(repr);
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "destroying an inert representation object failed\n");
            return 1;
        }
        return 0;
    }
    return 2;
}
