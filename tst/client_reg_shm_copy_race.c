/* A wl_shm window that changes colour on the scenario's go-files: green,
 * then blue, then red, each a fresh buffer committed whole. The scenario
 * reads the window right after each commit, while the compositor's CPU copy
 * of it is still running. */
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

static struct wl_buffer* recolor(struct wl_surface* surface, uint32_t argb) {
    struct wl_buffer* buffer = wl_solid(400, 300, argb);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, 400, 300);
    wl_surface_commit(surface);
    wl_display_roundtrip(wl_dpy);

    return buffer;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(120);

    if (wl_boot()) {
        return 1;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "shm-copy-race", 400, 300, 0xff00ff00u);
    printf("green\n");
    wait_go("blue");

    struct wl_buffer* blue = recolor(top.surface, 0xff0000ffu);

    printf("blue\n");
    wait_go("red");

    struct wl_buffer* red = recolor(top.surface, 0xffff0000u);

    printf("red\n");
    wait_go("done");
    wl_buffer_destroy(red);
    wl_buffer_destroy(blue);
    printf("shm copy race done\n");

    return 0;
}
