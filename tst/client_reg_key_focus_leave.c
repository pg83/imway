// Regression: a key held across a focus switch. Window A holds the focus
// while KEY_A goes down; clicking window B must deliver A's leave (leave
// implies all-released per the spec) and B's enter must CARRY the still
// pressed key in its keys array; the physical release then arrives as a
// normal key event.

#include "wl_util.h"

static struct wl_toplevel_ctx a, b;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    // A first: B maps on top, so A's top-left corner and B's bottom-right
    // corner stay mutually clear for the scenario's clicks
    wl_make_toplevel(&a, "kfA", 300, 200, 0xFFFF0000);
    wl_make_toplevel(&b, "kfB", 300, 200, 0xFF0000FF);
    printf("ready\n");

    // the scenario clicks A, presses KEY_A and holds it
    wlk_watch_key = 30;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (wlk_focus != a.surface) { fprintf(stderr, "key did not land on A\n"); return 1; }
    printf("held on A\n");

    // click on B: leave(A) then enter(B) with KEY_A in the array
    int leaves = wlk_leaves;
    while (wlk_focus != b.surface && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (wlk_leaves <= leaves) { fprintf(stderr, "no leave for A\n"); return 1; }

    int carried = 0;
    for (int i = 0; i < wlk_enter_nkeys; i++)
        if (wlk_enter_keys[i] == 30) carried = 1;
    if (!carried) {
        fprintf(stderr, "enter carried %d keys, KEY_A missing\n", wlk_enter_nkeys);
        return 1;
    }
    printf("enter carried the held key\n");

    // the physical release arrives as a key event on B
    int hits = wlk_watch_hits;
    while (wlk_watch_hits <= hits && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("release delivered\n");
    return 0;
}
