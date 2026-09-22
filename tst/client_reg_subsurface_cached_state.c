// A sync subsurface caches every piece of its committed state, not only the
// buffer, until the parent commits. Each step commits the child alone,
// prints "cached N", waits for KEY_1, commits the parent and prints
// "applied N":
//   1. a 200x160 green buffer at scale 2 with an explicit normal transform,
//      a 50x40 viewport source crop, an opaque region, an alpha multiplier
//      of one half and an RGB colour representation: once applied, a 50x40
//      half-green-over-red patch
//   2. a single-pixel magenta buffer stretched to 60x60 by the viewport
//      destination, source crop removed, full alpha: a 60x60 magenta square
//   3. a null buffer: the child unmaps

#include "wl_util.h"

#include <linux/input-event-codes.h>

#include <viewporter-client-protocol.h>
#include <alpha-modifier-v1-client-protocol.h>
#include <color-representation-v1-client-protocol.h>
#include <single-pixel-buffer-v1-client-protocol.h>

static struct wp_viewporter* viewporter;
static struct wp_alpha_modifier_v1* alpha_mod;
static struct wp_color_representation_manager_v1* representation;
static struct wp_single_pixel_buffer_manager_v1* spb;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    else if (!strcmp(iface, wp_alpha_modifier_v1_interface.name))
        alpha_mod = wl_registry_bind(registry, name, &wp_alpha_modifier_v1_interface, 1);
    else if (!strcmp(iface, wp_color_representation_manager_v1_interface.name))
        representation = wl_registry_bind(registry, name,
                                          &wp_color_representation_manager_v1_interface, 1);
    else if (!strcmp(iface, wp_single_pixel_buffer_manager_v1_interface.name))
        spb = wl_registry_bind(registry, name, &wp_single_pixel_buffer_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_toplevel_ctx top;
static struct wl_surface* child;

static void step(int n) {
    wl_surface_commit(child);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "step %d: the child commit failed\n", n);
        exit(1);
    }
    printf("cached %d\n", n);

    wlk_watch_key = KEY_1;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "step %d: the parent commit failed\n", n);
        exit(1);
    }
    printf("applied %d\n", n);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!wl_subcomp || !viewporter || !alpha_mod || !representation || !spb) {
        fprintf(stderr, "missing globals\n");
        return 1;
    }

    wl_make_toplevel(&top, "cachedstate", 300, 200, 0xFFFF0000);

    child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    wl_subsurface_set_position(sub, 40, 40);
    struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, child);
    struct wp_alpha_modifier_surface_v1* alpha = wp_alpha_modifier_v1_get_surface(alpha_mod, child);
    struct wp_color_representation_surface_v1* rep =
        wp_color_representation_manager_v1_get_surface(representation, child);

    // 1: everything but the buffer's pixels shapes what the parent shows
    wl_surface_attach(child, wl_solid(200, 160, 0xFF00FF00), 0, 0);
    wl_surface_damage(child, 0, 0, 200, 160);
    wl_surface_set_buffer_scale(child, 2);
    wl_surface_set_buffer_transform(child, WL_OUTPUT_TRANSFORM_NORMAL);
    wp_viewport_set_source(vp, wl_fixed_from_int(0), wl_fixed_from_int(0),
                           wl_fixed_from_int(50), wl_fixed_from_int(40));
    struct wl_region* opaque = wl_compositor_create_region(wl_comp);
    wl_region_add(opaque, 0, 0, 50, 40);
    wl_surface_set_opaque_region(child, opaque);
    wl_region_destroy(opaque);
    wp_alpha_modifier_surface_v1_set_multiplier(alpha, UINT32_MAX / 2);
    wp_color_representation_surface_v1_set_coefficients_and_range(
        rep, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY,
        WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL);
    step(1);

    // 2: a single-pixel buffer, sized by the viewport destination
    wl_surface_attach(child, wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
                                 spb, UINT32_MAX, 0, UINT32_MAX, UINT32_MAX), 0, 0);
    wl_surface_damage(child, 0, 0, 60, 60);
    wl_surface_set_buffer_scale(child, 1);
    wp_viewport_set_source(vp, wl_fixed_from_int(-1), wl_fixed_from_int(-1),
                           wl_fixed_from_int(-1), wl_fixed_from_int(-1));
    wp_viewport_set_destination(vp, 60, 60);
    wp_alpha_modifier_surface_v1_set_multiplier(alpha, UINT32_MAX);
    step(2);

    // 3: a null buffer unmaps the child
    wl_surface_attach(child, NULL, 0, 0);
    step(3);

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
