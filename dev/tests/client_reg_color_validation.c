// Negative color-management-v1 conformance cases.  Every mode runs in its
// own connection because the expected protocol error is fatal to the client.

#include "wl_util.h"

#include <errno.h>
#include <color-management-v1-client-protocol.h>

static struct wp_color_manager_v1* color_mgr;
static int image_ready;

static void image_failed(void* d, struct wp_image_description_v1* desc, uint32_t cause,
                         const char* message) {
    (void)d; (void)desc; (void)cause; (void)message;
}

static void image_ready_ev(void* d, struct wp_image_description_v1* desc, uint32_t identity) {
    (void)d; (void)desc; (void)identity;
    image_ready = 1;
}

static const struct wp_image_description_v1_listener image_listener = {
    .failed = image_failed,
    .ready = image_ready_ev,
};

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* interface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(interface, wp_color_manager_v1_interface.name))
        color_mgr = wl_registry_bind(registry, name, &wp_color_manager_v1_interface, 1);
}

static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}

static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int expect_error(const char* interface_name, uint32_t want_code) {
    (void)wl_display_roundtrip(wl_dpy);
    const struct wl_interface* interface = NULL;
    uint32_t object_id = 0;
    uint32_t code = wl_display_get_protocol_error(wl_dpy, &interface, &object_id);
    // create() destroys the creator proxy client-side while marshalling.  If
    // the server rejects that request, libwayland can no longer recover the
    // interface pointer, but it still preserves the protocol error code.
    int creator_was_destroyed = !interface &&
        !strcmp(interface_name, wp_image_description_creator_params_v1_interface.name);
    if (wl_display_get_error(wl_dpy) != EPROTO ||
        (!creator_was_destroyed && (!interface || strcmp(interface->name, interface_name))) ||
        code != want_code) {
        fprintf(stderr, "wrong/no error: %s code %u, want %s code %u (errno=%d)\n",
                interface ? interface->name : "?", code, interface_name, want_code,
                wl_display_get_error(wl_dpy));
        return 1;
    }
    printf("error ok: %s code %u\n", interface_name, want_code);
    return 0;
}

static struct wp_image_description_creator_params_v1* make_params(void) {
    return wp_color_manager_v1_create_parametric_creator(color_mgr);
}

static void set_required(struct wp_image_description_creator_params_v1* params) {
    wp_image_description_creator_params_v1_set_tf_named(
        params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
    wp_image_description_creator_params_v1_set_primaries_named(
        params, WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    if (argc != 2 || wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!color_mgr) return 2;

    if (!strcmp(argv[1], "unsupported-icc")) {
        wp_color_manager_v1_create_icc_creator(color_mgr);
        return expect_error(wp_color_manager_v1_interface.name,
                            WP_COLOR_MANAGER_V1_ERROR_UNSUPPORTED_FEATURE);
    }

    struct wp_image_description_creator_params_v1* params = make_params();

    if (!strcmp(argv[1], "unsupported-tf-power")) {
        wp_image_description_creator_params_v1_set_tf_power(params, 22000);
        return expect_error(wp_image_description_creator_params_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_UNSUPPORTED_FEATURE);
    }

    if (!strcmp(argv[1], "unsupported-primaries")) {
        wp_image_description_creator_params_v1_set_primaries(
            params, 640000, 330000, 300000, 600000, 150000, 60000, 312700, 329000);
        return expect_error(wp_image_description_creator_params_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_UNSUPPORTED_FEATURE);
    }

    if (!strcmp(argv[1], "duplicate-luminances")) {
        wp_image_description_creator_params_v1_set_luminances(params, 2000, 80, 80);
        wp_image_description_creator_params_v1_set_luminances(params, 2000, 100, 100);
        return expect_error(wp_image_description_creator_params_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
    }

    if (!strcmp(argv[1], "invalid-luminances")) {
        wp_image_description_creator_params_v1_set_tf_named(
            params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
        wp_image_description_creator_params_v1_set_luminances(params, 10000, 1, 80);
        return expect_error(wp_image_description_creator_params_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE);
    }

    if (!strcmp(argv[1], "unsupported-mastering-primaries")) {
        wp_image_description_creator_params_v1_set_mastering_display_primaries(
            params, 640000, 330000, 300000, 600000, 150000, 60000, 312700, 329000);
        return expect_error(wp_image_description_creator_params_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_UNSUPPORTED_FEATURE);
    }

    if (!strcmp(argv[1], "unsupported-mastering-luminance")) {
        wp_image_description_creator_params_v1_set_mastering_luminance(params, 10000, 1);
        return expect_error(wp_image_description_creator_params_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_UNSUPPORTED_FEATURE);
    }

    if (!strcmp(argv[1], "invalid-max-cll") || !strcmp(argv[1], "invalid-max-fall")) {
        set_required(params);
        wp_image_description_creator_params_v1_set_luminances(params, 0, 100, 80);
        if (!strcmp(argv[1], "invalid-max-cll")) {
            wp_image_description_creator_params_v1_set_max_cll(params, 101);
        } else {
            wp_image_description_creator_params_v1_set_max_cll(params, 80);
            wp_image_description_creator_params_v1_set_max_fall(params, 90);
        }
        wp_image_description_creator_params_v1_create(params);
        return expect_error(wp_image_description_creator_params_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE);
    }

    if (!strcmp(argv[1], "parametric-information")) {
        set_required(params);
        struct wp_image_description_v1* desc =
            wp_image_description_creator_params_v1_create(params);
        wp_image_description_v1_add_listener(desc, &image_listener, NULL);
        wl_display_roundtrip(wl_dpy);
        if (!image_ready) return 2;
        wp_image_description_v1_get_information(desc);
        return expect_error(wp_image_description_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_V1_ERROR_NO_INFORMATION);
    }

    return 2;
}
