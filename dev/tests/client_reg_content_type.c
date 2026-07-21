// wp-content-type: the compositor must advertise the global and record the
// hint on commit.

#include "wl_util.h"
#include <content-type-v1-client-protocol.h>

static struct wp_content_type_manager_v1* ct_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_content_type_manager_v1_interface.name))
        ct_mgr = wl_registry_bind(r, name, &wp_content_type_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!ct_mgr) {
        fprintf(stderr, "no wp_content_type_manager\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "content-type", 200, 150, 0xFF3060A0u);

    struct wp_content_type_v1* ct =
        wp_content_type_manager_v1_get_surface_content_type(ct_mgr, top.surface);
    wp_content_type_v1_set_content_type(ct, WP_CONTENT_TYPE_V1_TYPE_VIDEO);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_content_type: set video\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
