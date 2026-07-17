// Protocol conformance gaps which used to be silently accepted: decoration
// object uniqueness/lifetime/mode, color-management uniqueness/inertness and
// creator validation, and cursor-shape enum validation.

#include "wl_util.h"

#include <errno.h>
#include <color-management-v1-client-protocol.h>
#include <cursor-shape-v1-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>

static struct zxdg_decoration_manager_v1* deco_mgr;
static struct wp_color_manager_v1* color_mgr;
static struct wp_cursor_shape_manager_v1* cursor_mgr;
static int image_ready;

static void image_failed(void* d, struct wp_image_description_v1* z, uint32_t cause,
                         const char* msg) {
    (void)d; (void)z; (void)cause; (void)msg;
}
static void image_ready_ev(void* d, struct wp_image_description_v1* z, uint32_t id) {
    (void)d; (void)z; (void)id; image_ready = 1;
}
static const struct wp_image_description_v1_listener image_listener = {
    image_failed, image_ready_ev,
};

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                         uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
    else if (!strcmp(iface, wp_color_manager_v1_interface.name))
        color_mgr = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
    else if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
        cursor_mgr = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t name) {
    (void)d; (void)r; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int expect_error(const char* want_iface, uint32_t want_code) {
    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    const struct wl_interface* iface = NULL;
    uint32_t object_id = 0;
    uint32_t code = wl_display_get_protocol_error(wl_dpy, &iface, &object_id);
    if (wl_display_get_error(wl_dpy) != EPROTO || !iface ||
        strcmp(iface->name, want_iface) || code != want_code) {
        fprintf(stderr, "wrong/no error: %s code %u, want %s code %u (errno=%d)\n",
                iface ? iface->name : "?", code, want_iface, want_code,
                wl_display_get_error(wl_dpy));
        return 1;
    }
    printf("error ok: %s code %u\n", want_iface, want_code);
    return 0;
}

static void bind_extra(void) {
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (argc != 2 || wl_boot()) return 2;
    bind_extra();

    if (!strcmp(argv[1], "cm-invalid-tf") || !strcmp(argv[1], "cm-duplicate-tf") ||
        !strcmp(argv[1], "cm-invalid-prim") || !strcmp(argv[1], "cm-duplicate-prim")) {
        if (!color_mgr) return 2;
        struct wp_image_description_creator_params_v1* p =
            wp_color_manager_v1_create_parametric_creator(color_mgr);
        if (!strcmp(argv[1], "cm-invalid-tf")) {
            wp_image_description_creator_params_v1_set_tf_named(p, 0xdeadbeef);
            return expect_error(wp_image_description_creator_params_v1_interface.name,
                                WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_TF);
        }
        if (!strcmp(argv[1], "cm-duplicate-tf")) {
            wp_image_description_creator_params_v1_set_tf_named(
                p, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
            wp_image_description_creator_params_v1_set_tf_named(
                p, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
            return expect_error(wp_image_description_creator_params_v1_interface.name,
                                WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
        }
        if (!strcmp(argv[1], "cm-invalid-prim")) {
            wp_image_description_creator_params_v1_set_primaries_named(p, 0xdeadbeef);
            return expect_error(wp_image_description_creator_params_v1_interface.name,
                                WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_PRIMARIES_NAMED);
        }
        wp_image_description_creator_params_v1_set_primaries_named(
            p, WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
        wp_image_description_creator_params_v1_set_primaries_named(
            p, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);
        return expect_error(wp_image_description_creator_params_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
    }

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);

    if (!strcmp(argv[1], "cm-duplicate-surface")) {
        if (!color_mgr) return 2;
        wp_color_manager_v1_get_surface(color_mgr, surface);
        wp_color_manager_v1_get_surface(color_mgr, surface);
        return expect_error(wp_color_manager_v1_interface.name,
                            WP_COLOR_MANAGER_V1_ERROR_SURFACE_EXISTS);
    }

    if (!strcmp(argv[1], "cm-inert")) {
        if (!color_mgr) return 2;
        struct wp_color_management_surface_v1* cms =
            wp_color_manager_v1_get_surface(color_mgr, surface);
        wl_surface_destroy(surface);
        wp_color_management_surface_v1_unset_image_description(cms);
        return expect_error(wp_color_management_surface_v1_interface.name,
                            WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_INERT);
    }

    if (!strcmp(argv[1], "cm-invalid-intent")) {
        if (!color_mgr) return 2;
        struct wp_color_management_surface_v1* cms =
            wp_color_manager_v1_get_surface(color_mgr, surface);
        struct wp_image_description_creator_params_v1* p =
            wp_color_manager_v1_create_parametric_creator(color_mgr);
        wp_image_description_creator_params_v1_set_tf_named(
            p, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
        wp_image_description_creator_params_v1_set_primaries_named(
            p, WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
        struct wp_image_description_v1* desc = wp_image_description_creator_params_v1_create(p);
        wp_image_description_v1_add_listener(desc, &image_listener, NULL);
        while (!image_ready && wl_display_roundtrip(wl_dpy) >= 0) {
        }
        if (!image_ready) return 2;
        wp_color_management_surface_v1_set_image_description(cms, desc, 0xdeadbeef);
        return expect_error(wp_color_management_surface_v1_interface.name,
                            WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_RENDER_INTENT);
    }

    if (!strcmp(argv[1], "cursor-invalid") || !strcmp(argv[1], "cursor-stale")) {
        if (!cursor_mgr || !wl_ptr) return 2;
        struct wp_cursor_shape_device_v1* dev =
            wp_cursor_shape_manager_v1_get_pointer(cursor_mgr, wl_ptr);
        struct wl_toplevel_ctx top;
        wl_make_toplevel(&top, "client_reg_protocol_gaps", 400, 300, 0xFFFF0000);
        printf("client_reg_protocol_gaps: mapped\n");
        for (int i = 0; i < 300 && !wlp_enter_serial; i++) {
            if (wl_display_roundtrip(wl_dpy) < 0) break;
            usleep(20000);
        }
        if (!wlp_enter_serial) return 2;

        if (!strcmp(argv[1], "cursor-stale")) {
            wp_cursor_shape_device_v1_set_shape(
                dev, wlp_enter_serial + 1, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
            if (wl_display_roundtrip(wl_dpy) < 0) return 1;
            printf("client_reg_protocol_gaps: stale sent\n");
            while (wl_display_dispatch(wl_dpy) != -1) {
            }
            return 0;
        }

        wp_cursor_shape_device_v1_set_shape(dev, wlp_enter_serial, 0xdeadbeef);
        return expect_error(wp_cursor_shape_device_v1_interface.name,
                            WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE);
    }

    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    if (!deco_mgr) return 2;
    struct zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, tl);

    if (!strcmp(argv[1], "deco-duplicate")) {
        zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, tl);
        return expect_error(zxdg_toplevel_decoration_v1_interface.name,
                            ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED);
    }
    if (!strcmp(argv[1], "deco-invalid-mode")) {
        zxdg_toplevel_decoration_v1_set_mode(deco, 0xdeadbeef);
        return expect_error(zxdg_toplevel_decoration_v1_interface.name,
                            ZXDG_TOPLEVEL_DECORATION_V1_ERROR_INVALID_MODE);
    }
    if (!strcmp(argv[1], "deco-orphan")) {
        xdg_toplevel_destroy(tl);
        return expect_error(zxdg_toplevel_decoration_v1_interface.name,
                            ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ORPHANED);
    }

    return 2;
}
