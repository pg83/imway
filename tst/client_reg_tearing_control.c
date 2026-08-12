// wp-tearing-control: the compositor must advertise the global and record
// the async presentation hint on commit.

#include "wl_util.h"
#include <tearing-control-v1-client-protocol.h>

static struct wp_tearing_control_manager_v1* tc_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_tearing_control_manager_v1_interface.name))
        tc_mgr = wl_registry_bind(r, name, &wp_tearing_control_manager_v1_interface, 1);
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
    if (!tc_mgr) {
        fprintf(stderr, "no wp_tearing_control_manager\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "tearing", 200, 150, 0xFF80A020u);

    struct wp_tearing_control_v1* tc =
        wp_tearing_control_manager_v1_get_tearing_control(tc_mgr, top.surface);
    wp_tearing_control_v1_set_presentation_hint(tc, WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_tearing_control: async\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
