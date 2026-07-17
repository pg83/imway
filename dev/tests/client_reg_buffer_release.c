// Regression: wl_buffer release accounting over rapid commits. Three shm
// buffers committed back to back must all come back released (shm copies
// synchronously), and re-attaching the first must still work — a leaked or
// double release shows up here.

#include "wl_util.h"

static int releases;

static void buf_release(void* d, struct wl_buffer* b) {
    (void)d; (void)b;
    releases++;
}
static const struct wl_buffer_listener buf_listener = {buf_release};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "bufrel", 200, 150, 0xFF808080);

    struct wl_buffer* bufs[3];
    uint32_t colors[3] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF};
    for (int i = 0; i < 3; i++) {
        bufs[i] = wl_solid(200, 150, colors[i]);
        wl_buffer_add_listener(bufs[i], &buf_listener, NULL);
    }

    // three commits in one burst, no waiting in between
    for (int i = 0; i < 3; i++) {
        wl_surface_attach(top.surface, bufs[i], 0, 0);
        wl_surface_damage(top.surface, 0, 0, 200, 150);
        wl_surface_commit(top.surface);
    }
    for (int i = 0; i < 100 && releases < 3; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (releases != 3) {
        fprintf(stderr, "%d releases after the burst, want 3\n", releases);
        return 1;
    }
    printf("burst released\n");

    // the first buffer must be reusable: yellow it and commit again
    // (repaint through a fresh buffer since wl_solid unmapped the pixels)
    struct wl_buffer* again = wl_solid(200, 150, 0xFFFFFF00);
    wl_buffer_add_listener(again, &buf_listener, NULL);
    wl_surface_attach(top.surface, bufs[0], 0, 0);
    wl_surface_damage(top.surface, 0, 0, 200, 150);
    wl_surface_commit(top.surface);
    wl_surface_attach(top.surface, again, 0, 0);
    wl_surface_damage(top.surface, 0, 0, 200, 150);
    wl_surface_commit(top.surface);
    for (int i = 0; i < 100 && releases < 5; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (releases != 5) {
        fprintf(stderr, "%d releases after reuse, want 5\n", releases);
        return 1;
    }
    printf("reuse released\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
