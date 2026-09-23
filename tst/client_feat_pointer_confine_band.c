// Pointer confinement to a region of two disjoint rectangles, (20,60) and
// (180,60), each 100x80, in a 300x200 window. Every motion the client sees
// while confined is printed, and flagged if it lies outside both.

#include "wl_util.h"
#include <pointer-constraints-unstable-v1-client-protocol.h>

static struct zwp_pointer_constraints_v1* constraints;
static int confined;

static void on_confined(void* d, struct zwp_confined_pointer_v1* c) {
    (void)d;
    (void)c;
    confined = 1;
    printf("confined\n");
}

static void on_unconfined(void* d, struct zwp_confined_pointer_v1* c) {
    (void)d;
    (void)c;
    printf("unconfined\n");
}

static const struct zwp_confined_pointer_v1_listener confine_listener = {on_confined, on_unconfined};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, zwp_pointer_constraints_v1_interface.name))
        constraints = wl_registry_bind(r, name, &zwp_pointer_constraints_v1_interface, 1);
}

static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}

static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

static int inside(double x, double y) {
    return y >= 60 && y < 140 && ((x >= 20 && x < 120) || (x >= 180 && x < 280));
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(55);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!constraints || !wl_ptr) { fprintf(stderr, "missing globals\n"); return 1; }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "confine-band", 300, 200, 0xFFFF0000);

    struct wl_region* region = wl_compositor_create_region(wl_comp);
    wl_region_add(region, 20, 60, 100, 80);
    wl_region_add(region, 180, 60, 100, 80);
    struct zwp_confined_pointer_v1* conf = zwp_pointer_constraints_v1_confine_pointer(constraints, top.surface, wl_ptr, region, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_confined_pointer_v1_add_listener(conf, &confine_listener, NULL);
    wl_region_destroy(region);
    wl_surface_commit(top.surface);
    printf("ready\n");

    while (!confined && wl_display_dispatch(wl_dpy) != -1) {
    }

    int seen = wlp_motion_count;

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (wlp_motion_count != seen) {
            double x = wl_fixed_to_double(wlp_x), y = wl_fixed_to_double(wlp_y);

            seen = wlp_motion_count;
            printf("motion %.1f %.1f%s\n", x, y, inside(x, y) ? "" : " escaped");
        }
    }

    return 0;
}
