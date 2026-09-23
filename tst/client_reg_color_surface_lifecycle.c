// A color-management surface object over its whole life, none of which may
// cost the client its connection: unset on a surface that never had a
// description, destroyed before anything was set, destroyed after its
// wl_surface (the object is inert then, and destroy is still allowed), and a
// surface taking a fresh object after the old one is gone.

#include "wl_util.h"
#include <color-management-v1-client-protocol.h>

static struct wp_color_manager_v1* cm;

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

static int step(const char* what) {
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "%s: the connection failed (error %d)\n", what, wl_display_get_error(wl_dpy));
        return 1;
    }

    printf("%s: ok\n", what);
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 1;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!cm) return 1;

    struct wl_surface* a = wl_compositor_create_surface(wl_comp);
    struct wp_color_management_surface_v1* cms = wp_color_manager_v1_get_surface(cm, a);

    wp_color_management_surface_v1_unset_image_description(cms);
    wl_surface_commit(a);
    wp_color_management_surface_v1_destroy(cms);
    wl_surface_commit(a);
    if (step("unset with nothing set")) return 1;

    struct wl_surface* b = wl_compositor_create_surface(wl_comp);

    cms = wp_color_manager_v1_get_surface(cm, b);
    wp_color_management_surface_v1_destroy(cms);
    wl_surface_commit(b);
    if (step("destroyed before any set")) return 1;

    cms = wp_color_manager_v1_get_surface(cm, b);
    wp_color_management_surface_v1_unset_image_description(cms);
    wl_surface_commit(b);
    if (step("a fresh object on the same surface")) return 1;
    wp_color_management_surface_v1_destroy(cms);

    struct wl_surface* c = wl_compositor_create_surface(wl_comp);

    cms = wp_color_manager_v1_get_surface(cm, c);
    wl_display_roundtrip(wl_dpy);
    wl_surface_destroy(c);
    wl_display_roundtrip(wl_dpy);
    wp_color_management_surface_v1_destroy(cms);
    if (step("destroyed after its wl_surface")) return 1;

    wl_surface_destroy(a);
    wl_surface_destroy(b);
    wl_display_roundtrip(wl_dpy);
    printf("color surface lifecycle done\n");
    return 0;
}
