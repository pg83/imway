/* Surface damage the compositor cannot take literally, so it takes the whole
 * surface instead. At buffer scale 2, each side of the damage rectangle
 * alone overflows 32 bits once scaled, in both directions; on a turned
 * buffer the damage is not mapped at all. In both phases the new content
 * must show over the whole window, not just where the damage pointed. The
 * scenario reads the window's colour between phases, released by go-files. */
#include "wl_util.h"

#include <limits.h>

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

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) return 1;

    struct wl_toplevel_ctx top;

    // 400x300 at scale 2: a 200x150 window, blue to start with
    wl_make_toplevel(&top, "damage-edges", 400, 300, 0xff0000ffu);
    wl_surface_set_buffer_scale(top.surface, 2);
    wl_surface_attach(top.surface, wl_solid(400, 300, 0xff0000ffu), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 200, 150);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("blue\n");
    wait_go("blue");

    // red, damaged only by rectangles that overflow once scaled
    const int32_t big = INT32_MAX / 2 + 1, small = INT32_MIN / 2 - 1;

    wl_surface_attach(top.surface, wl_solid(400, 300, 0xffff0000u), 0, 0);
    wl_surface_damage(top.surface, small, 0, 1, 1);
    wl_surface_damage(top.surface, big, 0, 1, 1);
    wl_surface_damage(top.surface, 0, small, 1, 1);
    wl_surface_damage(top.surface, 0, big, 1, 1);
    wl_surface_damage(top.surface, 0, 0, small, 1);
    wl_surface_damage(top.surface, 0, 0, big, 1);
    wl_surface_damage(top.surface, 0, 0, 1, small);
    wl_surface_damage(top.surface, 0, 0, 1, big);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("red\n");
    wait_go("red");

    // green, turned half a circle, damaged in one corner pixel
    wl_surface_set_buffer_transform(top.surface, WL_OUTPUT_TRANSFORM_180);
    wl_surface_attach(top.surface, wl_solid(400, 300, 0xff00ff00u), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 1, 1);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("green\n");
    wait_go("green");

    printf("damage edges done\n");

    return 0;
}
