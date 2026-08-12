// Regression: confined-pointer regions retain their exact shape and set_region
// is double-buffered. Two separated rectangles must not turn into one bounding
// box; a replacement region must wait for wl_surface.commit.

#include "wl_util.h"
#include <linux/input-event-codes.h>
#include <pointer-constraints-unstable-v1-client-protocol.h>

static struct zwp_pointer_constraints_v1* constraints;
static struct zwp_confined_pointer_v1* confined_pointer;
static struct wl_toplevel_ctx top;
static int confined;

static void on_confined(void* data, struct zwp_confined_pointer_v1* pointer) {
    (void)data; (void)pointer;
    confined = 1;
    printf("region confined\n");
}

static void on_unconfined(void* data, struct zwp_confined_pointer_v1* pointer) {
    (void)data; (void)pointer;
}

static const struct zwp_confined_pointer_v1_listener confined_listener = {
    on_confined, on_unconfined,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, zwp_pointer_constraints_v1_interface.name))
        constraints = wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1);
}

static void registry_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global, registry_remove,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    if (wl_boot()) return 1;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!constraints || !wl_ptr) {
        fprintf(stderr, "missing pointer constraints\n");
        return 1;
    }

    wl_make_toplevel(&top, "constraint-region-commit", 300, 200, 0xFFFF0000);

    struct wl_region* initial = wl_compositor_create_region(wl_comp);
    wl_region_add(initial, 0, 0, 80, 200);
    wl_region_add(initial, 220, 0, 80, 200);
    confined_pointer = zwp_pointer_constraints_v1_confine_pointer(
        constraints, top.surface, wl_ptr, initial,
        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_confined_pointer_v1_add_listener(confined_pointer, &confined_listener, NULL);
    wl_region_destroy(initial);
    wl_surface_commit(top.surface);
    printf("region ready\n");

    while (!confined && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wl_region* replacement = wl_compositor_create_region(wl_comp);
    wl_region_add(replacement, 0, 0, 80, 200);
    zwp_confined_pointer_v1_set_region(confined_pointer, replacement);
    wl_region_destroy(replacement);
    printf("region pending\n");

    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    double before_commit = wl_fixed_to_double(wlp_x);
    if (before_commit < 219.0) {
        fprintf(stderr, "pending/disjoint region escaped: x=%.1f\n", before_commit);
        return 1;
    }

    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("region committed\n");

    wlk_watch_key = KEY_2;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    double after_commit = wl_fixed_to_double(wlp_x);
    if (after_commit > 80.0) {
        fprintf(stderr, "committed region not applied: x=%.1f\n", after_commit);
        return 1;
    }

    printf("constraint region commit ok\n");
    return 0;
}
