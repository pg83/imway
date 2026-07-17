// Regression: pointer-lock lifecycle reset. Lock the pointer on window A,
// destroy A with the lock ACTIVE (the risky order), then lock again on
// window B — the second lock must activate like the first. KEY_1 triggers
// the destruction.

#include "wl_util.h"
#include <linux/input-event-codes.h>
#include <pointer-constraints-unstable-v1-client-protocol.h>

static struct zwp_pointer_constraints_v1* constraints;
static int locked_count, unlocked_count;

static void on_locked(void* d, struct zwp_locked_pointer_v1* l) {
    (void)d; (void)l;
    locked_count++;
    printf("locked %d\n", locked_count);
}
static void on_unlocked(void* d, struct zwp_locked_pointer_v1* l) {
    (void)d; (void)l;
    unlocked_count++;
    printf("unlocked %d\n", unlocked_count);
}
static const struct zwp_locked_pointer_v1_listener lock_listener = {on_locked, on_unlocked};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_pointer_constraints_v1_interface.name))
        constraints = wl_registry_bind(r, name, &zwp_pointer_constraints_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!constraints || !wl_ptr) { fprintf(stderr, "missing globals\n"); return 1; }

    struct wl_toplevel_ctx a, b;
    wl_make_toplevel(&b, "lockB", 200, 150, 0xFF00FF00);
    wl_make_toplevel(&a, "lockA", 200, 150, 0xFFFF0000);

    struct zwp_locked_pointer_v1* lock = zwp_pointer_constraints_v1_lock_pointer(
        constraints, a.surface, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_locked_pointer_v1_add_listener(lock, &lock_listener, NULL);
    wl_surface_commit(a.surface);
    printf("ready\n");

    // the scenario moves the pointer onto A: the lock activates
    while (locked_count < 1 && wl_display_dispatch(wl_dpy) != -1) {
    }

    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    // destroy the locked window under the ACTIVE lock, lock resource after
    xdg_toplevel_destroy(a.tl);
    xdg_surface_destroy(a.xs);
    wl_surface_destroy(a.surface);
    wl_display_roundtrip(wl_dpy);
    zwp_locked_pointer_v1_destroy(lock);
    wl_display_roundtrip(wl_dpy);
    printf("destroyed under lock\n");

    // second lock on B; the scenario moves the pointer onto B
    struct zwp_locked_pointer_v1* lock2 = zwp_pointer_constraints_v1_lock_pointer(
        constraints, b.surface, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_locked_pointer_v1_add_listener(lock2, &lock_listener, NULL);
    wl_surface_commit(b.surface);

    while (locked_count < 2 && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("relock ok\n");
    return 0;
}
