// wl_surface v7 get_release: the compositor must advertise wl_surface >= 7
// and fire the per-commit release callback when the committed buffer is
// released. Exercised on an shm buffer (released at commit).

#define REG_COMPOSITOR_VERSION 7
#include "wl_util.h"

static struct wl_surface* surface;
static int configured, released, committed;

static void release_done(void* d, struct wl_callback* c, uint32_t data) {
    (void)d; (void)c; (void)data;
    released = 1;
}
static const struct wl_callback_listener release_listener = {release_done};

static void commit_frame(void) {
    struct wl_callback* rel = wl_surface_get_release(surface);
    wl_callback_add_listener(rel, &release_listener, NULL);
    wl_surface_attach(surface, wl_solid(200, 150, 0xFF3090C0u), 0, 0);
    wl_surface_damage(surface, 0, 0, 200, 150);
    wl_surface_commit(surface);
    committed = 1;
}

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    configured = 1;
    if (!committed) commit_frame();
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    if (wl_compositor_get_version(wl_comp) < 7) {
        fprintf(stderr, "wl_compositor at version %u, want >= 7 for get_release\n",
                wl_compositor_get_version(wl_comp));
        return 1;
    }

    surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &wl_tl_listener, NULL);
    xdg_toplevel_set_title(tl, "release-cb");
    xdg_toplevel_set_app_id(tl, "release-cb");
    wl_surface_commit(surface);

    for (int i = 0; i < 200 && !released; i++) {
        if (wl_display_dispatch(wl_dpy) < 0) break;
    }

    if (!released) {
        fprintf(stderr, "get_release callback never fired\n");
        return 1;
    }

    printf("client_reg_buffer_release_callback: release fired\n");
    return 0;
}
