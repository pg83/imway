/* PLAN buffer lifetime #8: destroy the wl_shm_pool right after creating the
 * buffer, then keep committing that buffer; every commit must be uploaded and
 * released. */
#include "wl_util.h"

static int releases;

static void buf_release(void* d, struct wl_buffer* b) {
    (void)d; (void)b;
    releases++;
}
static const struct wl_buffer_listener buf_listener = {buf_release};

int main(void) {
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "pool-destroy-reuse", 64, 64, 0xff808080);

    /* wl_solid destroys the pool immediately after create_buffer */
    struct wl_buffer* buf = wl_solid(64, 64, 0xff00ff00);
    wl_buffer_add_listener(buf, &buf_listener, NULL);
    for (int i = 0; i < 3; i++) {
        wl_surface_attach(top.surface, buf, 0, 0);
        wl_surface_damage(top.surface, 0, 0, 64, 64);
        wl_surface_commit(top.surface);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
    }
    for (int i = 0; i < 100 && releases < 3; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }
    if (releases < 3) {
        fprintf(stderr, "%d releases, want 3\n", releases);
        return 1;
    }
    return 0;
}
