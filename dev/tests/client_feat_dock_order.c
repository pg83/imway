#include "wl_util.h"

// two toplevels with distinct app_ids; the scenario drives focus between
// them and asserts the compositor's dock MRU order through the state dump
struct win {
    struct wl_surface* surface;
    struct xdg_surface* xs;
    struct xdg_toplevel* tl;
    uint32_t color;
    const char* name;
    int mapped;
};

static void xs_configure(void* data, struct xdg_surface* xdg, uint32_t serial) {
    struct win* w = data;

    xdg_surface_ack_configure(xdg, serial);
    if (!w->mapped) {
        wl_surface_attach(w->surface, wl_solid(300, 200, w->color), 0, 0);
        wl_surface_damage(w->surface, 0, 0, 300, 200);
        wl_surface_commit(w->surface);
        w->mapped = 1;
        printf("%s mapped\n", w->name);
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* data, struct xdg_toplevel* top, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)data; (void)top; (void)w; (void)h; (void)states;
}
static void tl_close(void* data, struct xdg_toplevel* top) { (void)data; (void)top; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void map(struct win* w, const char* app_id, uint32_t color) {
    w->name = app_id;
    w->color = color;
    w->surface = wl_compositor_create_surface(wl_comp);
    w->xs = xdg_wm_base_get_xdg_surface(wl_wm, w->surface);
    xdg_surface_add_listener(w->xs, &xs_listener, w);
    w->tl = xdg_surface_get_toplevel(w->xs);
    xdg_toplevel_add_listener(w->tl, &tl_listener, w);
    xdg_toplevel_set_app_id(w->tl, app_id);
    xdg_toplevel_set_title(w->tl, app_id);
    wl_surface_commit(w->surface);

    while (!w->mapped && wl_display_dispatch(wl_dpy) >= 0) {
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    static struct win a, b;

    map(&a, "order-a", 0xffff0000);
    map(&b, "order-b", 0xff00ff00);

    while (wl_display_dispatch(wl_dpy) >= 0) {
    }
    return 0;
}
