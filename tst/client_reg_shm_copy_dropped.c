// A green toplevel with a desynchronized subsurface at (50,50) that commits
// a 100x100 red wl_shm buffer ("red committed") and, once the scenario's
// go-file releases it, attaches no buffer at all ("dropped").

#include "wl_util.h"

static void wait_go(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);

    for (int i = 0; i < 3000; i++) {
        if (access(path, F_OK) == 0) {
            return;
        }

        usleep(20000);
        wl_display_roundtrip(wl_dpy);
    }

    fprintf(stderr, "the scenario never released %s\n", name);
    exit(1);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(120);

    if (wl_boot() || !wl_subcomp) {
        return 1;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "shm-copy-dropped", 300, 200, 0xff00ff00u);
    printf("green\n");
    wait_go("red");

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    struct wl_buffer* red = wl_solid(100, 100, 0xffff0000u);

    wl_subsurface_set_position(sub, 50, 50);
    wl_subsurface_set_desync(sub);
    wl_surface_commit(top.surface);
    wl_surface_attach(child, red, 0, 0);
    wl_surface_damage_buffer(child, 0, 0, 100, 100);
    wl_surface_commit(child);
    wl_display_roundtrip(wl_dpy);
    printf("red committed\n");
    wait_go("drop");

    wl_surface_attach(child, NULL, 0, 0);
    wl_surface_commit(child);
    wl_display_roundtrip(wl_dpy);
    printf("dropped\n");
    wait_go("done");
    wl_buffer_destroy(red);
    wl_subsurface_destroy(sub);
    wl_surface_destroy(child);
    printf("shm copy dropped done\n");

    return 0;
}
