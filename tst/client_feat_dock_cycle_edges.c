// Four windows for the dock's cycle action: one of app "dock-one", two of
// app "dock-two" (and a third that never maps), and one that never sets an
// app_id. Exits on "done-go" in XDG_RUNTIME_DIR.

#include "wl_util.h"

static int go(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", getenv("XDG_RUNTIME_DIR"), name);

    while (access(path, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "connection lost\n");
            return 0;
        }
        usleep(20000);
    }

    return 1;
}

static struct wl_toplevel_ctx tops[3];
static struct wl_surface* bare;
static struct xdg_surface* bare_xs;
static struct xdg_toplevel* bare_tl;
static int bare_configured;

static void bare_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!bare_configured) {
        wl_surface_attach(bare, wl_solid(160, 100, 0xFF808020u), 0, 0);
        wl_surface_damage(bare, 0, 0, 160, 100);
    }
    wl_surface_commit(bare);
    bare_configured = 1;
}
static const struct xdg_surface_listener bare_xs_listener = {bare_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    wl_make_toplevel(&tops[0], "dock-one", 160, 100, 0xFF2060A0u);
    wl_make_toplevel(&tops[1], "dock-two", 160, 100, 0xFF20A060u);
    wl_make_toplevel(&tops[2], "dock-two", 160, 100, 0xFFA02060u);

    // no set_app_id: the dock groups it under an empty id
    bare = wl_compositor_create_surface(wl_comp);
    bare_xs = xdg_wm_base_get_xdg_surface(wl_wm, bare);
    xdg_surface_add_listener(bare_xs, &bare_xs_listener, NULL);
    bare_tl = xdg_surface_get_toplevel(bare_xs);
    xdg_toplevel_set_title(bare_tl, "dock-bare");
    wl_surface_commit(bare);
    while (!bare_configured && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);

    // a third "dock-two" toplevel that never maps: the group's cycle skips it
    struct wl_surface* pending = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* pending_xs = xdg_wm_base_get_xdg_surface(wl_wm, pending);
    struct xdg_toplevel* pending_tl = xdg_surface_get_toplevel(pending_xs);
    xdg_toplevel_set_app_id(pending_tl, "dock-two");
    xdg_toplevel_set_title(pending_tl, "dock-two-pending");
    wl_surface_commit(pending);
    wl_display_roundtrip(wl_dpy);
    puts("client_feat_dock_cycle_edges: mapped");

    if (!go("done-go")) return 2;
    puts("client_feat_dock_cycle_edges: done");
    return 0;
}
