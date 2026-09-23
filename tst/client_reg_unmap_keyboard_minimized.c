// Three overlapping toplevels mapped bottom, middle, top. A keyboard of its
// own prints every wl_keyboard.enter as "keyboard enter bottom|middle|top",
// so an enter followed at once by a leave is seen too. SIGUSR1 minimizes the
// top one, SIGUSR2 unmaps the middle one with a null buffer, or with the
// argument "destroy" destroys its xdg_toplevel instead.

#include "wl_util.h"

#include <poll.h>
#include <signal.h>

static struct wl_toplevel_ctx bottom, middle, top;
static volatile sig_atomic_t minimizeRequested, unmapRequested;

static void onUsr1(int sig) {
    (void)sig;
    minimizeRequested = 1;
}

static void onUsr2(int sig) {
    (void)sig;
    unmapRequested = 1;
}

static void kb_keymap(void* d, struct wl_keyboard* k, uint32_t format, int32_t fd, uint32_t size) {
    (void)d; (void)k; (void)format; (void)size;
    close(fd);
}
static void kb_enter(void* d, struct wl_keyboard* k, uint32_t serial, struct wl_surface* s, struct wl_array* keys) {
    (void)d; (void)k; (void)serial; (void)keys;
    printf("keyboard enter %s\n", s == bottom.surface ? "bottom" : s == middle.surface ? "middle" : s == top.surface ? "top" : "other");
}
static void kb_leave(void* d, struct wl_keyboard* k, uint32_t serial, struct wl_surface* s) {
    (void)d; (void)k; (void)serial; (void)s;
}
static void kb_key(void* d, struct wl_keyboard* k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state) {
    (void)d; (void)k; (void)serial; (void)t; (void)key; (void)state;
}
static void kb_mods(void* d, struct wl_keyboard* k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
    (void)d; (void)k; (void)serial; (void)dep; (void)lat; (void)lock; (void)group;
}
static void kb_repeat(void* d, struct wl_keyboard* k, int32_t rate, int32_t delay) {
    (void)d; (void)k; (void)rate; (void)delay;
}
static const struct wl_keyboard_listener kb_listener = {kb_keymap, kb_enter, kb_leave, kb_key, kb_mods, kb_repeat};

int main(int argc, char** argv) {
    int destroy = argc > 1 && !strcmp(argv[1], "destroy");

    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGUSR1, onUsr1);
    signal(SIGUSR2, onUsr2);
    alarm(55);

    if (wl_boot()) return 1;

    struct wl_keyboard* kb = wl_seat_get_keyboard(wl_seat_g);
    wl_keyboard_add_listener(kb, &kb_listener, NULL);

    wl_make_toplevel(&bottom, "unmapkb-bottom", 300, 300, 0xFFFF0000);
    wl_make_toplevel(&middle, "unmapkb-middle", 300, 300, 0xFF00FF00);
    wl_make_toplevel(&top, "unmapkb-top", 300, 300, 0xFF0000FF);
    printf("windows mapped\n");

    struct pollfd pfd = {wl_display_get_fd(wl_dpy), POLLIN, 0};
    int minimized = 0, unmapped = 0;

    for (;;) {
        if (minimizeRequested && !minimized) {
            minimized = 1;
            xdg_toplevel_set_minimized(top.tl);
            wl_surface_commit(top.surface);
            printf("minimize requested\n");
        }

        if (unmapRequested && !unmapped) {
            unmapped = 1;

            if (destroy) {
                xdg_toplevel_destroy(middle.tl);
                xdg_surface_destroy(middle.xs);
                middle.tl = NULL;
                middle.xs = NULL;
            } else {
                wl_surface_attach(middle.surface, NULL, 0, 0);
                wl_surface_commit(middle.surface);
            }

            printf("unmap requested\n");
        }

        wl_display_flush(wl_dpy);

        if (wl_display_prepare_read(wl_dpy) != 0) {
            if (wl_display_dispatch_pending(wl_dpy) < 0) return 0;
            continue;
        }

        if (poll(&pfd, 1, 100) > 0) {
            if (wl_display_read_events(wl_dpy) < 0) return 0;
        } else {
            wl_display_cancel_read(wl_dpy);
        }

        if (wl_display_dispatch_pending(wl_dpy) < 0) return 0;
    }
}
