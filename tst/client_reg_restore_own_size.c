// A server-side-decorated window that fills the maximized size it is
// configured with, but picks a size of its own once unmaximized: 240x160,
// not the 300x200 it had and the compositor suggests back (a floating
// configure's size is only a suggestion). Steps wait for go-files in
// XDG_RUNTIME_DIR: "max-go" asks to be maximized, "unmax-go" to be
// unmaximized, "done-go" exits.

#include "wl_util.h"

#include <xdg-decoration-unstable-v1-client-protocol.h>

static struct zxdg_decoration_manager_v1* deco_mgr;
static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static int cfg_w, cfg_h, cfg_max, unmaximizing, drawn_w, drawn_h;

static void draw(int w, int h) {
    if (w == drawn_w && h == drawn_h) {
        wl_surface_commit(surface);
        return;
    }
    wl_surface_attach(surface, wl_solid(w, h, 0xFF30A030u), 0, 0);
    wl_surface_damage(surface, 0, 0, w, h);
    wl_surface_commit(surface);
    drawn_w = w;
    drawn_h = h;
    printf("client_reg_restore_own_size: drew %dx%d\n", w, h);
}

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d; (void)t;
    uint32_t* state;
    cfg_w = w;
    cfg_h = h;
    cfg_max = 0;
    wl_array_for_each(state, states) {
        if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED) cfg_max = 1;
    }
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (cfg_max && cfg_w > 0 && cfg_h > 0) {
        draw(cfg_w, cfg_h);
    } else if (unmaximizing) {
        draw(240, 160);
    } else {
        draw(300, 200);
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

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

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!deco_mgr) { fprintf(stderr, "no xdg-decoration manager\n"); return 1; }

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "restore-own");
    xdg_toplevel_set_app_id(tl, "restore-own");

    struct zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, tl);
    zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    wl_surface_commit(surface);

    while (!drawn_w && wl_display_dispatch(wl_dpy) >= 0) {
    }
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_restore_own_size: mapped\n");

    if (!go("max-go")) return 2;
    xdg_toplevel_set_maximized(tl);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_restore_own_size: maximize asked\n");

    if (!go("unmax-go")) return 3;
    unmaximizing = 1;
    xdg_toplevel_unset_maximized(tl);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_restore_own_size: unmaximize asked\n");

    if (!go("done-go")) return 4;
    zxdg_toplevel_decoration_v1_destroy(deco);
    xdg_toplevel_destroy(tl);
    xdg_surface_destroy(xs);
    wl_surface_destroy(surface);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_restore_own_size: done\n");
    return 0;
}
