#include "wl_util.h"

// A wl_shm cursor image the device cannot upload: a green cursor buffer
// set on pointer enter, then committed again unchanged once the scenario's
// go-file releases it.

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

static void show(struct wl_surface* cursor, struct wl_buffer* buffer) {
    wl_surface_attach(cursor, buffer, 0, 0);
    wl_surface_damage_buffer(cursor, 0, 0, 24, 24);
    wl_surface_commit(cursor);
    wl_display_roundtrip(wl_dpy);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot() || !wl_ptr) {
        return 2;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "cursor-shm-fault", 300, 200, 0xffff0000u);
    printf("cursor shm fault mapped\n");

    while (!wlp_enter_count && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    struct wl_buffer* green = wl_solid(24, 24, 0xff00ff00u);

    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor, 0, 0);
    show(cursor, green);
    printf("green cursor set\n");
    wait_go("again");
    show(cursor, green);
    printf("green cursor again\n");
    wait_go("done");
    wl_buffer_destroy(green);
    wl_surface_destroy(cursor);
    printf("cursor shm fault done\n");

    return 0;
}
