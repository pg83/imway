// wp-alpha-modifier: a compositor-side opacity multiplier. A fully opaque
// green surface set to multiplier 0 must blend away to the background.

#include "wl_util.h"
#include <alpha-modifier-v1-client-protocol.h>

static struct wp_alpha_modifier_v1* alpha_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_alpha_modifier_v1_interface.name))
        alpha_mgr = wl_registry_bind(r, name, &wp_alpha_modifier_v1_interface, 1);
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
    if (!alpha_mgr) {
        fprintf(stderr, "no wp_alpha_modifier\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "alpha-mod", 300, 200, 0xFF00FF00u);
    printf("client_reg_alpha_modifier: opaque\n");

    struct wp_alpha_modifier_surface_v1* am =
        wp_alpha_modifier_v1_get_surface(alpha_mgr, top.surface);

    // wait for the scenario to capture the opaque frame (it injects a key)
    wlk_watch_key = 57; // KEY_SPACE
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    // fully transparent
    wp_alpha_modifier_surface_v1_set_multiplier(am, 0);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_alpha_modifier: transparent\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
