// A focused window whose app_id is longer than the top bar keeps: 300
// characters, "a" to "z" over and over. Exits once "done-go" appears in
// XDG_RUNTIME_DIR.

#include "wl_util.h"

static struct wl_toplevel_ctx top;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) return 1;

    wl_make_toplevel(&top, "bar-long-app-id", 200, 120, 0xFFFF0000);

    char id[301];

    for (int i = 0; i < 300; i++) {
        id[i] = (char)('a' + i % 26);
    }

    id[300] = 0;
    xdg_toplevel_set_app_id(top.tl, id);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    puts("long app id set");

    char path[512];

    snprintf(path, sizeof(path), "%s/done-go", getenv("XDG_RUNTIME_DIR"));

    while (access(path, F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 2;
        usleep(20000);
    }

    return 0;
}
