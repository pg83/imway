#include "wl_util.h"

#include <alpha-modifier-v1-client-protocol.h>
#include <content-type-v1-client-protocol.h>
#include <fractional-scale-v1-client-protocol.h>
#include <keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h>
#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <xdg-output-unstable-v1-client-protocol.h>

// A per-surface extension object destroyed while its surface lives lets go
// of the surface: a second one for the same surface is no protocol error
// (content type, alpha modifier, fractional scale, shortcuts inhibitor).
// The other ends: an xdg_output destroyed on request, a locked pointer
// given a cursor hint and a null region, and a constraint and a content type
// whose surface is gone taking a region, a type and their own destroy
// without complaint.

static struct wp_content_type_manager_v1* content_type;
static struct wp_alpha_modifier_v1* alpha;
static struct wp_fractional_scale_manager_v1* frac;
static struct zwp_keyboard_shortcuts_inhibit_manager_v1* inhibit;
static struct zwp_pointer_constraints_v1* constraints;
static struct zxdg_output_manager_v1* xdg_outputs;
static struct wl_output* output;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_content_type_manager_v1_interface.name))
        content_type = wl_registry_bind(r, name, &wp_content_type_manager_v1_interface, 1);
    else if (!strcmp(iface, wp_alpha_modifier_v1_interface.name))
        alpha = wl_registry_bind(r, name, &wp_alpha_modifier_v1_interface, 1);
    else if (!strcmp(iface, wp_fractional_scale_manager_v1_interface.name))
        frac = wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name))
        inhibit = wl_registry_bind(r, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_pointer_constraints_v1_interface.name))
        constraints = wl_registry_bind(r, name, &zwp_pointer_constraints_v1_interface, 1);
    else if (!strcmp(iface, zxdg_output_manager_v1_interface.name))
        xdg_outputs = wl_registry_bind(r, name, &zxdg_output_manager_v1_interface, 3);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(r, name, &wl_output_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int step(const char* what) {
    if (wl_display_roundtrip(wl_dpy) < 0) {
        const struct wl_interface* iface = NULL;
        uint32_t code = wl_display_get_protocol_error(wl_dpy, &iface, NULL);

        fprintf(stderr, "%s: protocol error %s code %u\n", what, iface ? iface->name : "?", code);
        exit(1);
    }

    printf("%s: ok\n", what);

    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!content_type || !alpha || !frac || !inhibit || !constraints || !xdg_outputs || !output ||
        !wl_ptr || !wl_seat_g) {
        fprintf(stderr, "missing globals\n");
        return 2;
    }

    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "surface-ext-reget", 200, 200, 0xff406080u);

    struct wl_surface* s = ctx.surface;

    struct wp_content_type_v1* ct = wp_content_type_manager_v1_get_surface_content_type(content_type, s);
    wp_content_type_v1_set_content_type(ct, WP_CONTENT_TYPE_V1_TYPE_GAME);
    wl_surface_commit(s);
    wp_content_type_v1_destroy(ct);
    wl_surface_commit(s);
    wp_content_type_manager_v1_get_surface_content_type(content_type, s);
    step("content type again");

    struct wp_alpha_modifier_surface_v1* am = wp_alpha_modifier_v1_get_surface(alpha, s);
    wp_alpha_modifier_surface_v1_set_multiplier(am, UINT32_MAX / 2);
    wl_surface_commit(s);
    wp_alpha_modifier_surface_v1_destroy(am);
    wl_surface_commit(s);
    wp_alpha_modifier_v1_get_surface(alpha, s);
    step("alpha modifier again");

    wp_fractional_scale_v1_destroy(wp_fractional_scale_manager_v1_get_fractional_scale(frac, s));
    wp_fractional_scale_manager_v1_get_fractional_scale(frac, s);
    step("fractional scale again");

    zwp_keyboard_shortcuts_inhibitor_v1_destroy(
        zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(inhibit, s, wl_seat_g));
    zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(inhibit, s, wl_seat_g);
    step("shortcuts inhibitor again");

    zxdg_output_v1_destroy(zxdg_output_manager_v1_get_xdg_output(xdg_outputs, output));
    step("xdg output destroyed");

    struct zwp_locked_pointer_v1* lock = zwp_pointer_constraints_v1_lock_pointer(
        constraints, s, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_locked_pointer_v1_set_cursor_position_hint(lock, wl_fixed_from_int(10), wl_fixed_from_int(10));
    zwp_locked_pointer_v1_set_region(lock, NULL);
    wl_surface_commit(s);
    zwp_locked_pointer_v1_destroy(lock);
    step("locked pointer hinted");

    // a constraint outliving its surface
    struct wl_surface* doomed = wl_compositor_create_surface(wl_comp);
    struct zwp_confined_pointer_v1* confine = zwp_pointer_constraints_v1_confine_pointer(
        constraints, doomed, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    step("confined");
    wl_surface_destroy(doomed);
    step("constraint surface gone");
    zwp_confined_pointer_v1_set_region(confine, NULL);
    zwp_confined_pointer_v1_destroy(confine);
    step("orphan constraint destroyed");

    // a content type outliving its surface takes a type without complaint
    struct wl_surface* short_lived = wl_compositor_create_surface(wl_comp);
    struct wp_content_type_v1* orphan_ct =
        wp_content_type_manager_v1_get_surface_content_type(content_type, short_lived);

    wl_surface_destroy(short_lived);
    step("content type surface gone");
    wp_content_type_v1_set_content_type(orphan_ct, WP_CONTENT_TYPE_V1_TYPE_VIDEO);
    wp_content_type_v1_destroy(orphan_ct);
    step("orphan content type set");

    printf("surface extensions done\n");

    return 0;
}
