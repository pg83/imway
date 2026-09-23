// wp-pointer-warp requests the compositor must refuse: a serial that is not
// the pointer's enter, a surface of this client that the pointer is not on,
// and points left of, above, right of and below the focused surface. None
// may move the cursor (no motion at the requested point, no leave); a valid
// warp afterwards still does.
#include "wl_util.h"
#include <pointer-warp-v1-client-protocol.h>

static struct wp_pointer_warp_v1* warp;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_pointer_warp_v1_interface.name))
        warp = wl_registry_bind(r, name, &wp_pointer_warp_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void warp_to(struct wl_surface* s, int x, int y, uint32_t serial) {
    wp_pointer_warp_v1_warp_pointer(warp, s, wl_ptr, wl_fixed_from_int(x), wl_fixed_from_int(y), serial);
}

// true once a motion reports the pointer at (x, y) on our surface
static int reached(int x, int y) {
    return wlp_focus && abs(wl_fixed_to_int(wlp_x) - x) <= 2 && abs(wl_fixed_to_int(wlp_y) - y) <= 2;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;
    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!warp) {
        fprintf(stderr, "no wp_pointer_warp\n");
        return 1;
    }

    struct wl_toplevel_ctx other, top;
    // the other window maps first: the scenario aims at the later one
    wl_make_toplevel(&other, "pointer-warp-other", 120, 90, 0xFF804020u);
    wl_make_toplevel(&top, "pointer-warp-rejected", 300, 200, 0xFF208040u);
    printf("client_reg_pointer_warp_rejected: mapped\n");

    for (int i = 0; i < 300 && !(wlp_enter_count && wlp_focus == top.surface); i++) {
        if (wl_display_dispatch(wl_dpy) < 0) break;
    }
    if (wlp_focus != top.surface) {
        fprintf(stderr, "pointer never entered our surface\n");
        return 1;
    }

    // each of these, if accepted, would put the cursor at (150, 100) of the
    // named surface or take it off ours
    warp_to(top.surface, 150, 100, wlp_enter_serial + 1000);
    warp_to(other.surface, 60, 45, wlp_enter_serial);
    warp_to(top.surface, -5, 100, wlp_enter_serial);
    warp_to(top.surface, 150, -5, wlp_enter_serial);
    warp_to(top.surface, 300, 100, wlp_enter_serial);
    warp_to(top.surface, 150, 200, wlp_enter_serial);
    for (int i = 0; i < 20; i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(10000);
        if (reached(150, 100) || wlp_focus != top.surface) {
            fprintf(stderr, "a refused warp moved the cursor (%d,%d, focus %s)\n",
                    wl_fixed_to_int(wlp_x), wl_fixed_to_int(wlp_y), wlp_focus == top.surface ? "kept" : "lost");
            return 1;
        }
    }

    // the valid one still lands
    warp_to(top.surface, 150, 100, wlp_enter_serial);
    for (int i = 0; i < 200 && !reached(150, 100); i++) {
        if (wl_display_dispatch(wl_dpy) < 0) break;
    }
    if (!reached(150, 100)) {
        fprintf(stderr, "the valid warp after the refused ones did not land\n");
        return 1;
    }
    printf("client_reg_pointer_warp_rejected: refused and accepted as expected\n");
    return 0;
}
