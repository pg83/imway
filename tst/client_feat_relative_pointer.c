// Feature: relative-pointer. A focused client with a relative pointer object
// must receive relative_motion deltas when the pointer moves relatively.

#include "wl_util.h"
#include <relative-pointer-unstable-v1-client-protocol.h>

static struct zwp_relative_pointer_manager_v1* rel_mgr;
static struct wl_toplevel_ctx top;

static void rel_motion(void* d, struct zwp_relative_pointer_v1* rp, uint32_t th, uint32_t tl,
                       wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dxu, wl_fixed_t dyu) {
    (void)d; (void)rp; (void)th; (void)tl; (void)dxu; (void)dyu;
    wlrel_dx = wl_fixed_to_double(dx);
    wlrel_dy = wl_fixed_to_double(dy);
    wlrel_count++;
}
static const struct zwp_relative_pointer_v1_listener rel_listener = {rel_motion};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_relative_pointer_manager_v1_interface.name))
        rel_mgr = wl_registry_bind(r, name, &zwp_relative_pointer_manager_v1_interface, 1);
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
    if (!rel_mgr) { fprintf(stderr, "no relative-pointer manager\n"); return 1; }

    struct zwp_relative_pointer_v1* rp = zwp_relative_pointer_manager_v1_get_relative_pointer(rel_mgr, wl_ptr);
    zwp_relative_pointer_v1_add_listener(rp, &rel_listener, NULL);

    wl_make_toplevel(&top, "client_feat_relative_pointer", 400, 300, 0xFFFF0000);
    printf("client_feat_relative_pointer: mapped\n");

    // the scenario focuses the pointer on us, then injects relative motion
    for (int i = 0; i < 400; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        if (wlrel_count > 0) {
            printf("client_feat_relative_pointer: relative_motion dx=%.1f dy=%.1f (n=%d)\n",
                   wlrel_dx, wlrel_dy, wlrel_count);
            printf("client_feat_relative_pointer: ok\n");
            return 0;
        }
        usleep(20000);
    }
    fprintf(stderr, "client_feat_relative_pointer: no relative_motion\n");
    return 1;
}
