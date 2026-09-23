/* Window geometry that lies outside the surface: first a geometry inside
 * the 200x120 buffer (the window is its size), then one that starts past
 * the buffer's right and bottom edges, which leaves nothing of the surface
 * to crop to, so the window falls back to the whole surface. The scenario
 * reads the window size from the dump after each step. */
#include "wl_util.h"

static void wait_go(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);

    for (int i = 0; i < 1500; i++) {
        if (access(path, F_OK) == 0) {
            return;
        }

        usleep(20000);
        wl_display_roundtrip(wl_dpy);
    }

    fprintf(stderr, "the scenario never released %s\n", name);
    exit(1);
}

static void geometry(struct wl_toplevel_ctx* top, int x, int y, int w, int h) {
    xdg_surface_set_window_geometry(top->xs, x, y, w, h);
    wl_surface_attach(top->surface, wl_solid(200, 120, 0xff3080c0), 0, 0);
    wl_surface_damage(top->surface, 0, 0, 200, 120);
    wl_surface_commit(top->surface);
    wl_display_roundtrip(wl_dpy);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "geometry-outside", 200, 120, 0xff3080c0);
    geometry(&top, 10, 10, 50, 40);
    printf("inside\n");
    wait_go("inside");

    geometry(&top, 250, 150, 50, 40);
    printf("outside\n");
    wait_go("outside");

    printf("geometry outside done\n");

    return 0;
}
