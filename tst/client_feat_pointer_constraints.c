// Feature: pointer-constraints (lock) + relative-pointer. Locking the pointer
// on the focused surface must fire the locked event; while locked, injected
// motion arrives as relative deltas.

#include "wl_util.h"
#include <relative-pointer-unstable-v1-client-protocol.h>
#include <pointer-constraints-unstable-v1-client-protocol.h>

static struct zwp_relative_pointer_manager_v1* rel_mgr;
static struct zwp_pointer_constraints_v1* constraints;
static struct wl_toplevel_ctx top;
static int locked;

static void rel_motion(void* d, struct zwp_relative_pointer_v1* rp, uint32_t th, uint32_t tl,
                       wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dxu, wl_fixed_t dyu) {
    (void)d; (void)rp; (void)th; (void)tl; (void)dxu; (void)dyu;
    wlrel_dx = wl_fixed_to_double(dx);
    wlrel_dy = wl_fixed_to_double(dy);
    wlrel_count++;
}
static const struct zwp_relative_pointer_v1_listener rel_listener = {rel_motion};

static void lp_locked(void* d, struct zwp_locked_pointer_v1* lp) { (void)d; (void)lp; locked = 1; }
static void lp_unlocked(void* d, struct zwp_locked_pointer_v1* lp) { (void)d; (void)lp; }
static const struct zwp_locked_pointer_v1_listener lp_listener = {lp_locked, lp_unlocked};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_relative_pointer_manager_v1_interface.name))
        rel_mgr = wl_registry_bind(r, name, &zwp_relative_pointer_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_pointer_constraints_v1_interface.name))
        constraints = wl_registry_bind(r, name, &zwp_pointer_constraints_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ptr) { fprintf(stderr, "no pointer\n"); return 1; }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!rel_mgr || !constraints) { fprintf(stderr, "no rel-pointer / constraints\n"); return 1; }

    struct zwp_relative_pointer_v1* rp = zwp_relative_pointer_manager_v1_get_relative_pointer(rel_mgr, wl_ptr);
    zwp_relative_pointer_v1_add_listener(rp, &rel_listener, NULL);

    wl_make_toplevel(&top, "client_feat_pointer_constraints", 400, 300, 0xFFFF0000);

    struct zwp_locked_pointer_v1* lock = zwp_pointer_constraints_v1_lock_pointer(
        constraints, top.surface, wl_ptr, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_locked_pointer_v1_add_listener(lock, &lp_listener, NULL);
    printf("client_feat_pointer_constraints: mapped, lock requested\n");

    // the scenario focuses the pointer (activating the lock) and injects motion
    for (int i = 0; i < 400; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        if (locked && wlrel_count > 0) {
            printf("client_feat_pointer_constraints: locked, relative dx=%.1f dy=%.1f\n",
                   wlrel_dx, wlrel_dy);
            printf("client_feat_pointer_constraints: ok\n");
            return 0;
        }
        usleep(20000);
    }
    fprintf(stderr, "client_feat_pointer_constraints: locked=%d rel=%d\n", locked, wlrel_count);
    return 1;
}
