// A toplevel and its popup lose their wl_surfaces while their xdg objects
// live on: the compositor keeps them listed without a surface until the
// client destroys the role objects too. Stages are released by go-files.

#include "wl_util.h"

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) { (void)d; (void)p; }
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t t) { (void)d; (void)p; (void)t; }
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static int popup_acked;
static struct wl_surface* popup_surface;

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!popup_acked) {
        wl_surface_attach(popup_surface, wl_solid(60, 40, 0xFF00FF00), 0, 0);
        wl_surface_commit(popup_surface);
        popup_acked = 1;
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void stage(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);
    printf("stage %s\n", name);
    for (int i = 0; i < 1500; i++) {
        if (access(path, F_OK) == 0) {
            return;
        }
        usleep(20000);
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "disconnected at stage %s\n", name);
            exit(1);
        }
    }
    fprintf(stderr, "the scenario never released stage %s\n", name);
    exit(1);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 2;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "surface-gone-first", 240, 160, 0xFFFF0000);

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 60, 40);
    xdg_positioner_set_anchor_rect(pos, 10, 10, 20, 20);
    popup_surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* popup_xs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
    xdg_surface_add_listener(popup_xs, &popup_xs_listener, NULL);
    struct xdg_popup* popup = xdg_surface_get_popup(popup_xs, top.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_positioner_destroy(pos);
    wl_surface_commit(popup_surface);
    while (!popup_acked && wl_display_dispatch(wl_dpy) != -1) {
    }
    wl_display_roundtrip(wl_dpy);
    stage("mapped");

    wl_surface_destroy(popup_surface);
    wl_surface_destroy(top.surface);
    wl_display_roundtrip(wl_dpy);
    stage("surfaces-gone");

    xdg_popup_destroy(popup);
    xdg_surface_destroy(popup_xs);
    xdg_toplevel_destroy(top.tl);
    xdg_surface_destroy(top.xs);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "destroying the surfaceless role objects failed\n");
        return 1;
    }
    printf("roles gone\n");
    return 0;
}
