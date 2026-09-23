// Surface-coordinate damage (wl_surface.damage, not damage_buffer) on a
// window whose buffer transform is already in effect: a red 90-degree
// buffer maps, then a green one replaces it with damage given in surface
// coordinates. The scenario sees the green reach the screen; the go-files
// order the steps.

#include "wl_util.h"

static struct wl_surface* surface;
static int configured;

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    configured = 1;
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void wait_go(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);
    while (access(path, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) exit(1);
        usleep(20000);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 2;

    surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_set_title(tl, "damage-transformed");
    xdg_toplevel_set_app_id(tl, "damage-transformed");
    wl_surface_commit(surface);
    while (!configured && wl_display_dispatch(wl_dpy) != -1) {
    }

    // a 120x200 buffer turned 90 degrees: a 200x120 window
    wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_90);
    wl_surface_attach(surface, wl_solid(120, 200, 0xFFFF0000), 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, 120, 200);
    wl_surface_commit(surface);
    wl_display_roundtrip(wl_dpy);
    printf("red mapped\n");
    wait_go("green");

    wl_surface_attach(surface, wl_solid(120, 200, 0xFF00FF00), 0, 0);
    wl_surface_damage(surface, 0, 0, 200, 120);
    wl_surface_commit(surface);
    wl_display_roundtrip(wl_dpy);
    printf("green committed\n");
    wait_go("exit");
    return 0;
}
