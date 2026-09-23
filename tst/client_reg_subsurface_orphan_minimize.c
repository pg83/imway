// A window with a subsurface above it and one below, whose wl_surfaces are
// destroyed while their wl_subsurface objects are kept: the parent's stacks
// still hold both, with no surface behind them. The window is then
// minimized. Steps wait for go-files in XDG_RUNTIME_DIR: "destroy-go",
// "minimize-go", then "done-go" to exit.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_surface* children[2];
static struct wl_subsurface* subs[2];

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
    if (!wl_subcomp) { fprintf(stderr, "no subcompositor\n"); return 1; }

    wl_make_toplevel(&top, "orphan-subs", 300, 200, 0xFFFF0000);

    for (int i = 0; i < 2; i++) {
        children[i] = wl_compositor_create_surface(wl_comp);
        subs[i] = wl_subcompositor_get_subsurface(wl_subcomp, children[i], top.surface);
        wl_subsurface_set_position(subs[i], 20 + 60 * i, 20);
        wl_surface_attach(children[i], wl_solid(40, 40, 0xFF00FF00), 0, 0);
        wl_surface_damage(children[i], 0, 0, 40, 40);
        wl_surface_commit(children[i]);
    }

    wl_subsurface_place_below(subs[1], top.surface);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    puts("subs up");

    if (!go("destroy-go")) return 2;

    for (int i = 0; i < 2; i++) {
        wl_surface_destroy(children[i]);
        children[i] = NULL;
    }

    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    puts("surfaces gone");

    if (!go("minimize-go")) return 3;

    xdg_toplevel_set_minimized(top.tl);
    wl_display_roundtrip(wl_dpy);
    puts("minimize requested");

    if (!go("done-go")) return 4;

    for (int i = 0; i < 2; i++) {
        wl_subsurface_destroy(subs[i]);
    }

    wl_display_roundtrip(wl_dpy);
    puts("done");
    return 0;
}
