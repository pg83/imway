// Regression (#14): a keyboard-shortcuts inhibitor sets a global
// shortcutsInhibited flag. When the last inhibiting window closed while it
// held focus, the flag was never recomputed and stayed stuck true, so every
// compositor chord was dead forever. This client just arms the trap: open a
// focused window with an inhibitor, then close it (leaving no windows). The
// scenario then fires a global chord (Super+F2, the launcher) and checks it
// still works — mapping a new window here would itself recompute the flag and
// hide the bug, so we deliberately leave the compositor window-less.

#include "wl_util.h"
#include <keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h>

static struct zwp_keyboard_shortcuts_inhibit_manager_v1* inhibit_mgr;

static void mgr_reg_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                           uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name))
        inhibit_mgr = wl_registry_bind(r, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
}
static void mgr_reg_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener mgr_reg_listener = {mgr_reg_global, mgr_reg_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_seat_g) {
        fprintf(stderr, "client_reg_shortcuts_inhibit: no seat\n");
        return 1;
    }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &mgr_reg_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!inhibit_mgr) {
        fprintf(stderr, "client_reg_shortcuts_inhibit: no shortcuts-inhibit manager\n");
        return 1;
    }

    // a focused window that inhibits shortcuts → shortcutsInhibited = true
    struct wl_toplevel_ctx w1;
    wl_make_toplevel(&w1, "client_reg_shortcuts_inhibit", 400, 300, 0xFFFF0000);
    zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(inhibit_mgr, w1.surface, wl_seat_g);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_shortcuts_inhibit: inhibitor active\n");

    // Close only the window (destroy the wl_surface), leaving the inhibitor
    // resource alive so cleanup goes through kbInhibitSurfaceGone / toplevelGone
    // — the paths that forgot to recompute the flag — not the inhibitor's own
    // destroy handler (which always recomputed). No window is left focused.
    xdg_toplevel_destroy(w1.tl);
    xdg_surface_destroy(w1.xs);
    wl_surface_destroy(w1.surface);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_shortcuts_inhibit: inhibitor window closed\n");

    // stay connected so the inhibitor resource lives while the scenario tries
    // a global chord
    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
