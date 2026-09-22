// A 400x300 dark toplevel carrying a 40x30 grid of 8x8 subsurfaces, each on
// a 10-pixel pitch: 1200 surfaces with content, so 1200 textures and more
// descriptor sets than the renderer's first descriptor pool holds. All cells
// show one green shm buffer ("flood committed"); a press of KEY_A commits a
// blue buffer to every cell ("flood recommitted"). The toplevel is drawn
// (a frame callback) before the cells are created.

#include "wl_util.h"

#define COLS 40
#define ROWS 30

static int frame_done;

static void on_frame(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d;
    (void)t;
    wl_callback_destroy(cb);
    frame_done = 1;
}
static const struct wl_callback_listener frame_listener = {on_frame};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;
    if (!wl_subcomp) {
        fprintf(stderr, "no wl_subcompositor\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "texture-flood", 400, 300, 0xff202020);

    // the toplevel's own texture is built before any cell exists, so the
    // cells' GPU allocations come after all of the toplevel's
    struct wl_callback* cb = wl_surface_frame(top.surface);
    wl_callback_add_listener(cb, &frame_listener, NULL);
    wl_surface_commit(top.surface);
    while (!frame_done && wl_display_dispatch(wl_dpy) != -1) {
    }

    static struct wl_surface* cells[COLS * ROWS];
    struct wl_buffer* green = wl_solid(8, 8, 0xff00ff00);

    for (int i = 0; i < COLS * ROWS; i++) {
        cells[i] = wl_compositor_create_surface(wl_comp);
        struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, cells[i], top.surface);
        wl_subsurface_set_position(sub, (i % COLS) * 10 + 1, (i / COLS) * 10 + 1);
        wl_surface_attach(cells[i], green, 0, 0);
        wl_surface_damage(cells[i], 0, 0, 8, 8);
        wl_surface_commit(cells[i]);
    }
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("flood committed %d\n", COLS * ROWS);

    wlk_watch_key = 30; // KEY_A
    int recommitted = 0;

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (wlk_watch_hits && !recommitted) {
            struct wl_buffer* blue = wl_solid(8, 8, 0xff0000ff);

            for (int i = 0; i < COLS * ROWS; i++) {
                wl_surface_attach(cells[i], blue, 0, 0);
                wl_surface_damage(cells[i], 0, 0, 8, 8);
                wl_surface_commit(cells[i]);
            }
            wl_surface_commit(top.surface);
            wl_display_flush(wl_dpy);
            recommitted = 1;
            printf("flood recommitted\n");
        }
    }
    return 0;
}
