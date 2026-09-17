// A pointer confinement is not for keeps. Narrow its region to nothing and
// there is nowhere left to confine the pointer to, so the compositor has to
// end the confinement and say so rather than holding the cursor in an empty
// box.

#include "wl_util.h"

#include <pointer-constraints-unstable-v1-client-protocol.h>

static struct zwp_pointer_constraints_v1* constraints;
static int confined, unconfined;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_pointer_constraints_v1_interface.name))
        constraints = wl_registry_bind(r, name, &zwp_pointer_constraints_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void on_confined(void* d, struct zwp_confined_pointer_v1* p) {
    (void)d; (void)p;
    confined = 1;
}
static void on_unconfined(void* d, struct zwp_confined_pointer_v1* p) {
    (void)d; (void)p;
    unconfined = 1;
}
static const struct zwp_confined_pointer_v1_listener confine_listener = {on_confined, on_unconfined};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!constraints || !wl_ptr) {
        fprintf(stderr, "no pointer constraints (constraints=%p ptr=%p)\n",
                (void*)constraints, (void*)wl_ptr);
        return 2;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "pointer-unlock", 400, 300, 0xFF30A060u);
    printf("client_reg_pointer_unlock: mapped\n");

    // the scenario aims the pointer at us; the lock only engages on a
    // surface that has the pointer focus
    while (!wlp_enter_count && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct zwp_confined_pointer_v1* confine = zwp_pointer_constraints_v1_confine_pointer(
        constraints, top.surface, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);

    zwp_confined_pointer_v1_add_listener(confine, &confine_listener, NULL);

    while (!confined && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("client_reg_pointer_unlock: confined\n");

    // a region with nothing in it: the confinement has nowhere to hold the
    // pointer and has to end
    struct wl_region* empty = wl_compositor_create_region(wl_comp);

    zwp_confined_pointer_v1_set_region(confine, empty);
    wl_surface_commit(top.surface);
    wl_region_destroy(empty);

    while (!unconfined && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("client_reg_pointer_unlock: unconfined\n");

    return 0;
}
