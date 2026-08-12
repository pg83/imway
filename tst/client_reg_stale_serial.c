// Regression: xdg_toplevel.move serial validation. A move with a garbage
// serial must be ignored (the window must not follow a drag); the same move
// with the real press serial must work. First press fires the bad request,
// second press the good one.

#include "wl_util.h"
#include <xdg-decoration-unstable-v1-client-protocol.h>

static struct wl_toplevel_ctx top;
static struct zxdg_decoration_manager_v1* deco_mgr;

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);
    if (wl_boot()) return 1;

    // SSD: a CSD window has no title bar, and imgui would move it on a body
    // drag regardless of the move request — the test needs the drag inert
    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!deco_mgr) { fprintf(stderr, "no xdg-decoration manager\n"); return 1; }

    wl_make_toplevel(&top, "serial", 300, 200, 0xFFFF0000);
    struct zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, top.tl);
    zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("ready\n");

    int fired_bad = 0, fired_good = 0;
    while ((!fired_bad || !fired_good) && wl_display_dispatch(wl_dpy) != -1) {
        if (wlp_button_state != WL_POINTER_BUTTON_STATE_PRESSED) continue;
        if (!fired_bad && wlp_button_count >= 1) {
            xdg_toplevel_move(top.tl, wl_seat_g, wlp_button_serial + 9999);
            wl_display_flush(wl_dpy);
            fired_bad = 1;
            printf("bad move requested\n");
        } else if (fired_bad && !fired_good && wlp_button_count >= 3) {
            xdg_toplevel_move(top.tl, wl_seat_g, wlp_button_serial);
            wl_display_flush(wl_dpy);
            fired_good = 1;
            printf("good move requested\n");
        }
    }

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
