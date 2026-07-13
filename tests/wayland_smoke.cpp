// Смоук libwayland-server: display + сокет + один проход event loop.

#include <cstdio>
#include <cstdlib>

#include <wayland-server-core.h>

int main() {
    // add_socket_auto требует XDG_RUNTIME_DIR
    if (!getenv("XDG_RUNTIME_DIR")) {
        char tmpl[] = "/tmp/imway-smoke-XXXXXX";
        if (char* dir = mkdtemp(tmpl)) setenv("XDG_RUNTIME_DIR", dir, 1);
    }

    struct wl_display* dpy = wl_display_create();
    if (!dpy) { std::fprintf(stderr, "wl_display_create failed\n"); return 1; }

    const char* sock = wl_display_add_socket_auto(dpy);
    if (!sock) { std::fprintf(stderr, "add_socket_auto failed\n"); return 1; }
    std::printf("socket: %s\n", sock);

    struct wl_event_loop* loop = wl_display_get_event_loop(dpy);
    wl_event_loop_dispatch(loop, 0);
    wl_display_flush_clients(dpy);

    wl_display_destroy(dpy);
    std::printf("ok\n");
    return 0;
}
