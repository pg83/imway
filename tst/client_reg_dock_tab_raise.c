#include "wl_util.h"

#include <xdg-decoration-unstable-v1-client-protocol.h>

// two ssd toplevels (a title bar is what the scenario drags to dock them):
// "dock-anim" repaints on every frame callback and reports each one on
// stdout, "dock-static" is the docking drop target. The scenario tabs them
// into one dock node and watches the callback rate.

static struct zxdg_decoration_manager_v1* deco_mgr;
static int cb_count;
static struct wl_toplevel_ctx anim;
static struct wl_buffer* bufs[2];

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name))
        deco_mgr = wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

// wl_make_toplevel commits before a decoration request could sneak in, so
// build the toplevel by hand around the shared listeners
static void make_ssd_toplevel(struct wl_toplevel_ctx* c, const char* title, int w, int h, uint32_t color) {
    c->w = w;
    c->h = h;
    c->color = color;
    c->committed = 0;
    c->surface = wl_compositor_create_surface(wl_comp);
    c->xs = xdg_wm_base_get_xdg_surface(wl_wm, c->surface);
    xdg_surface_add_listener(c->xs, &wl_tl_xdg_listener, c);
    c->tl = xdg_surface_get_toplevel(c->xs);
    xdg_toplevel_add_listener(c->tl, &wl_tl_listener, c);
    xdg_toplevel_set_title(c->tl, title);
    xdg_toplevel_set_app_id(c->tl, title);

    struct zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration(deco_mgr, c->tl);
    zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

    wl_surface_commit(c->surface);
    while (!c->committed && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);
}

static void frame_done(void* data, struct wl_callback* callback, uint32_t msec);
static const struct wl_callback_listener frame_listener = {frame_done};

static void arm(void) {
    struct wl_callback* callback = wl_surface_frame(anim.surface);

    wl_callback_add_listener(callback, &frame_listener, NULL);
    wl_surface_attach(anim.surface, bufs[cb_count & 1], 0, 0);
    wl_surface_damage(anim.surface, 0, 0, 240, 160);
    wl_surface_commit(anim.surface);
}

static void frame_done(void* data, struct wl_callback* callback, uint32_t msec) {
    (void)data;
    (void)msec;
    wl_callback_destroy(callback);
    cb_count++;
    printf("cb %d\n", cb_count);
    fflush(stdout);
    arm();
}

int main(void) {
    alarm(120);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!deco_mgr) {
        fprintf(stderr, "no xdg-decoration manager\n");
        return 1;
    }

    struct wl_toplevel_ctx target;

    make_ssd_toplevel(&anim, "dock-anim", 240, 160, 0xffff2020);
    make_ssd_toplevel(&target, "dock-static", 240, 160, 0xff20c020);

    bufs[0] = wl_solid(240, 160, 0xffff2020);
    bufs[1] = wl_solid(240, 160, 0xffe01010);

    printf("two mapped\n");
    fflush(stdout);
    arm();

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
