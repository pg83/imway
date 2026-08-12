// Color state is double-buffered. set/unset must take effect only with the
// wl_surface.commit which follows it, and unset must restore the default sRGB
// path rather than leaving the old conversion cached.

#include "wl_util.h"
#include <color-management-v1-client-protocol.h>

static struct wp_color_manager_v1* cm;
static int desc_ready;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                         uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        cm = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t name) {
    (void)d; (void)r; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void desc_failed(void* d, struct wp_image_description_v1* z, uint32_t cause,
                        const char* msg) {
    (void)d; (void)z; (void)cause; (void)msg;
}
static void desc_ready_ev(void* d, struct wp_image_description_v1* z, uint32_t id) {
    (void)d; (void)z; (void)id; desc_ready = 1;
}
static const struct wp_image_description_v1_listener desc_listener = {
    desc_failed, desc_ready_ev,
};

static void wait_marker(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", getenv("XDG_RUNTIME_DIR"), name);
    while (access(path, F_OK) != 0) {
        wl_display_dispatch_pending(wl_dpy);
        wl_display_flush(wl_dpy);
        usleep(20000);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!cm) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "client_reg_color_commit", 300, 200, 0xFFB4783C);
    printf("color-commit: raw\n");

    struct wp_image_description_creator_params_v1* p =
        wp_color_manager_v1_create_parametric_creator(cm);
    wp_image_description_creator_params_v1_set_tf_named(
        p, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    wp_image_description_creator_params_v1_set_primaries_named(
        p, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);
    struct wp_image_description_v1* desc = wp_image_description_creator_params_v1_create(p);
    wp_image_description_v1_add_listener(desc, &desc_listener, NULL);
    while (!desc_ready && wl_display_roundtrip(wl_dpy) >= 0) {
    }
    if (!desc_ready) return 1;
    struct wp_color_management_surface_v1* cms =
        wp_color_manager_v1_get_surface(cm, top.surface);

    wait_marker("go-set");
    wp_color_management_surface_v1_set_image_description(
        cms, desc, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
    wl_display_flush(wl_dpy);
    printf("color-commit: pending-set\n");

    wait_marker("go-commit-set");
    wl_surface_commit(top.surface);
    wl_display_flush(wl_dpy);
    printf("color-commit: managed\n");

    wait_marker("go-unset");
    wp_color_management_surface_v1_unset_image_description(cms);
    wl_display_flush(wl_dpy);
    printf("color-commit: pending-unset\n");

    wait_marker("go-commit-unset");
    wl_surface_commit(top.surface);
    wl_display_flush(wl_dpy);
    printf("color-commit: unset\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
