// wp-pointer-warp: a focused client may place the cursor within its surface.
// After the scenario moves the pointer onto the window, the client warps to a
// known surface-local point and expects a motion event delivering it there.

#include "wl_util.h"
#include <pointer-warp-v1-client-protocol.h>

static struct wp_pointer_warp_v1* warp;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_pointer_warp_v1_interface.name))
        warp = wl_registry_bind(r, name, &wp_pointer_warp_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

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

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "pointer-warp", 300, 200, 0xFF208040u);
    printf("client_reg_pointer_warp: mapped\n");

    // wait for the scenario to move the pointer onto us
    for (int i = 0; i < 300 && !wlp_enter_count; i++) {
        if (wl_display_dispatch(wl_dpy) < 0) break;
    }
    if (!wlp_enter_count || wlp_focus != top.surface) {
        fprintf(stderr, "pointer never entered our surface\n");
        return 1;
    }

    int want_x = 120, want_y = 90;
    wlp_motion_count = 0;
    wp_pointer_warp_v1_warp_pointer(warp, top.surface, wl_ptr,
                                    wl_fixed_from_int(want_x), wl_fixed_from_int(want_y),
                                    wlp_enter_serial);
    wl_display_flush(wl_dpy);

    for (int i = 0; i < 200; i++) {
        if (wl_display_dispatch(wl_dpy) < 0) break;
        int px = wl_fixed_to_int(wlp_x), py = wl_fixed_to_int(wlp_y);
        if (wlp_motion_count && abs(px - want_x) <= 2 && abs(py - want_y) <= 2) {
            printf("client_reg_pointer_warp: warped to %d,%d\n", px, py);
            return 0;
        }
    }

    fprintf(stderr, "cursor not warped (last %d,%d, motions=%d)\n",
            wl_fixed_to_int(wlp_x), wl_fixed_to_int(wlp_y), wlp_motion_count);
    return 1;
}
