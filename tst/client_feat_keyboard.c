// Feature: wl_keyboard delivery. A focused surface must get a keymap, an
// enter, repeat_info (v4+), key events, and modifiers events reflecting a held
// modifier. The scenario presses Shift + a letter; the client asserts the
// modifier mask went non-zero while held and a key was delivered.

#include "wl_util.h"

static struct wl_toplevel_ctx top;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_kbd) { fprintf(stderr, "client_feat_keyboard: no keyboard\n"); return 1; }

    wl_make_toplevel(&top, "client_feat_keyboard", 400, 300, 0xFFFF0000);
    printf("client_feat_keyboard: mapped\n");

    // let keymap / enter / repeat_info settle
    for (int i = 0; i < 60 && !(wlk_enters && wlk_got_repeat && wlk_keymap_fd >= 0); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (wlk_keymap_fd < 0) { fprintf(stderr, "no keymap\n"); return 1; }
    if (!wlk_enters) { fprintf(stderr, "no keyboard enter\n"); return 1; }
    if (!wlk_got_repeat) { fprintf(stderr, "no repeat_info\n"); return 1; }

    wlk_watch_key = 30; // KEY_A — the scenario types it under Shift
    printf("client_feat_keyboard: ready\n");

    for (int i = 0; i < 300; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        if (wlk_watch_hits > 0 && wlk_mods_max_depressed != 0) {
            printf("client_feat_keyboard: key hits=%d mods_max=0x%x repeat rate=%d delay=%d\n",
                   wlk_watch_hits, wlk_mods_max_depressed, wlk_repeat_rate, wlk_repeat_delay);
            printf("client_feat_keyboard: ok\n");
            return 0;
        }
        usleep(20000);
    }

    fprintf(stderr, "client_feat_keyboard: key=%d mods_max=0x%x\n", wlk_watch_hits,
            wlk_mods_max_depressed);
    return 1;
}
