/* An input region built with the requests that change nothing next to one
 * that does: over a 200x150 window, the whole surface is added, then an
 * add with no height, an add pinned at the far bottom of the coordinate
 * space (clamped to nothing), a subtract with no width, and a subtract of
 * the top-left 100x75 that starts above and left of the surface. The
 * pointer must be over the window everywhere but that top-left quarter.
 * The client keeps the pointer's state in $XDG_RUNTIME_DIR/pointer-state
 * ("in" or "out") for the scenario to poll. */
#include "wl_util.h"

#include <limits.h>

static void write_state(const char* state) {
    char path[512], tmp[520];

    snprintf(path, sizeof(path), "%s/pointer-state", getenv("XDG_RUNTIME_DIR"));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE* f = fopen(tmp, "w");

    if (!f) {
        return;
    }

    fputs(state, f);
    fclose(f);
    rename(tmp, path);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot() || !wl_ptr) return 1;

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "input-region-edges", 200, 150, 0xffc03020);

    struct wl_region* region = wl_compositor_create_region(wl_comp);

    wl_region_add(region, 0, 0, 200, 150);
    wl_region_add(region, 10, 10, 50, 0);
    wl_region_add(region, 0, INT32_MAX, 10, 10);
    wl_region_subtract(region, 0, 0, 0, 50);
    wl_region_subtract(region, -20, -20, 120, 95);
    wl_surface_set_input_region(top.surface, region);
    wl_region_destroy(region);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);

    write_state("out");
    printf("region set\n");

    struct wl_surface* last = NULL;

    for (;;) {
        if (wl_display_roundtrip(wl_dpy) < 0) {
            return 1;
        }

        if (wlp_focus != last) {
            last = wlp_focus;
            write_state(last == top.surface ? "in" : "out");
        }

        usleep(10000);
    }
}
