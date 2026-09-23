// Pointer constraints across leaving and re-entering the surface.
//   1. a oneshot lock locks on entry and unlocks when the pointer leaves;
//      spent, it does not lock again on the next entry
//   2. a confinement whose region misses the surface never confines
//   3. with an input region set, a confinement confines to it; relative
//      motion then reaches no one (only another client has a relative
//      pointer), and the confinement destroyed while active lets go
// Each phase prints "phase N ready" and waits for the scenario to move the
// pointer out and back in.

#include "wl_util.h"
#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <relative-pointer-unstable-v1-client-protocol.h>

static struct zwp_pointer_constraints_v1* constraints;
static struct zwp_relative_pointer_manager_v1* rel_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_pointer_constraints_v1_interface.name))
        constraints = wl_registry_bind(r, name, &zwp_pointer_constraints_v1_interface, 1);
    else if (!strcmp(iface, zwp_relative_pointer_manager_v1_interface.name))
        rel_mgr = wl_registry_bind(r, name, &zwp_relative_pointer_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int locks, unlocks, confines, unconfines;

static void on_locked(void* d, struct zwp_locked_pointer_v1* l) {
    (void)d; (void)l;
    printf("locked %d\n", ++locks);
}
static void on_unlocked(void* d, struct zwp_locked_pointer_v1* l) {
    (void)d; (void)l;
    printf("unlocked %d\n", ++unlocks);
}
static const struct zwp_locked_pointer_v1_listener lock_listener = {on_locked, on_unlocked};

static void on_confined(void* d, struct zwp_confined_pointer_v1* c) {
    (void)d; (void)c;
    printf("confined %d\n", ++confines);
}
static void on_unconfined(void* d, struct zwp_confined_pointer_v1* c) {
    (void)d; (void)c;
    printf("unconfined %d\n", ++unconfines);
}
static const struct zwp_confined_pointer_v1_listener confine_listener = {on_confined, on_unconfined};

static int bystander_motions;

static void rel_motion(void* d, struct zwp_relative_pointer_v1* p, uint32_t hi, uint32_t lo,
                       wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t ux, wl_fixed_t uy) {
    (void)d; (void)p; (void)hi; (void)lo; (void)dx; (void)dy; (void)ux; (void)uy;
    bystander_motions++;
}
static const struct zwp_relative_pointer_v1_listener rel_listener = {rel_motion};

// wait for the pointer to leave ("phase N out") and enter again; everything
// the entry triggered arrives in the same batch, so one roundtrip settles it
static void await_reentry(int phase) {
    while (wlp_focus && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("phase %d out\n", phase);

    while (!wlp_focus && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_display_roundtrip(wl_dpy);
}

static struct wl_display* bystander;
static uint32_t by_seat_name, by_rel_name;

static void by_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)r; (void)v;
    if (!strcmp(iface, wl_seat_interface.name) && !by_seat_name)
        by_seat_name = name;
    else if (!strcmp(iface, zwp_relative_pointer_manager_v1_interface.name))
        by_rel_name = name;
}
static const struct wl_registry_listener by_listener = {by_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(90);

    if (wl_boot() || !wl_ptr) return 2;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!constraints || !rel_mgr) return 2;

    // the other client: a relative pointer of its own
    bystander = wl_display_connect(NULL);
    if (!bystander) return 2;

    struct wl_registry* by_reg = wl_display_get_registry(bystander);

    wl_registry_add_listener(by_reg, &by_listener, NULL);
    wl_display_roundtrip(bystander);

    struct wl_seat* by_seat = wl_registry_bind(by_reg, by_seat_name, &wl_seat_interface, 5);
    struct zwp_relative_pointer_manager_v1* by_rel =
        wl_registry_bind(by_reg, by_rel_name, &zwp_relative_pointer_manager_v1_interface, 1);

    zwp_relative_pointer_v1_add_listener(
        zwp_relative_pointer_manager_v1_get_relative_pointer(by_rel, wl_seat_get_pointer(by_seat)), &rel_listener, NULL);
    wl_display_roundtrip(bystander);

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "constraint-cycle", 300, 240, 0xffc02020u);

    // 1. oneshot lock
    struct zwp_locked_pointer_v1* lock = zwp_pointer_constraints_v1_lock_pointer(
        constraints, top.surface, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);

    zwp_locked_pointer_v1_add_listener(lock, &lock_listener, NULL);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("phase 1 ready\n");

    while (!unlocks && wl_display_dispatch(wl_dpy) != -1) {
    }

    await_reentry(1);

    if (locks != 1) {
        fprintf(stderr, "a spent oneshot lock locked again (%d locks)\n", locks);
        return 1;
    }

    printf("phase 1 done\n");
    zwp_locked_pointer_v1_destroy(lock);

    // 2. a confinement region nowhere on the surface
    struct wl_region* far = wl_compositor_create_region(wl_comp);

    wl_region_add(far, 5000, 5000, 10, 10);

    struct zwp_confined_pointer_v1* confine = zwp_pointer_constraints_v1_confine_pointer(
        constraints, top.surface, wl_ptr, far, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);

    zwp_confined_pointer_v1_add_listener(confine, &confine_listener, NULL);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("phase 2 ready\n");
    await_reentry(2);

    if (confines) {
        fprintf(stderr, "a region off the surface confined the pointer\n");
        return 1;
    }

    printf("phase 2 done\n");
    zwp_confined_pointer_v1_destroy(confine);

    // 3. confined to the input region, made while the pointer is away so
    // the next entry is the one that confines
    while (wlp_focus && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("phase 3 away\n");

    struct wl_region* input = wl_compositor_create_region(wl_comp);

    wl_region_add(input, 0, 0, 150, 150);
    wl_surface_set_input_region(top.surface, input);
    confine = zwp_pointer_constraints_v1_confine_pointer(
        constraints, top.surface, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_confined_pointer_v1_add_listener(confine, &confine_listener, NULL);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("phase 3 ready\n");

    while (!confines && wl_display_dispatch(wl_dpy) != -1) {
    }

    // the scenario sends relative motion now; the focused client has no
    // relative pointer, the other client's must not hear it
    while (access("go-destroy", F_OK) != 0 && wl_display_roundtrip(wl_dpy) >= 0) {
        usleep(20000);
    }

    wl_display_roundtrip(bystander);

    if (bystander_motions) {
        fprintf(stderr, "another client's relative pointer heard the motion\n");
        return 1;
    }

    zwp_confined_pointer_v1_destroy(confine);
    wl_display_roundtrip(wl_dpy);
    printf("constraint cycle done\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
