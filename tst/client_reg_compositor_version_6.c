// wl_compositor v6: the compositor must advertise version >= 6 and send
// preferred_buffer_scale / preferred_buffer_transform on a mapped surface,
// and honour the v5 wl_surface.offset request (attach with a non-zero offset
// is a protocol error at v5+).

#define REG_COMPOSITOR_VERSION 6
#include "wl_util.h"

static int got_scale, pref_scale, got_transform, pref_transform;

static void s_enter(void* d, struct wl_surface* s, struct wl_output* o) { (void)d;(void)s;(void)o; }
static void s_leave(void* d, struct wl_surface* s, struct wl_output* o) { (void)d;(void)s;(void)o; }
static void s_pref_scale(void* d, struct wl_surface* s, int32_t scale) {
    (void)d; (void)s; got_scale = 1; pref_scale = scale;
}
static void s_pref_transform(void* d, struct wl_surface* s, uint32_t transform) {
    (void)d; (void)s; got_transform = 1; pref_transform = transform;
}
static const struct wl_surface_listener s_listener = {
    .enter = s_enter, .leave = s_leave,
    .preferred_buffer_scale = s_pref_scale,
    .preferred_buffer_transform = s_pref_transform,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    if (wl_compositor_get_version(wl_comp) < 6) {
        fprintf(stderr, "wl_compositor at version %u, want >= 6\n",
                wl_compositor_get_version(wl_comp));
        return 1;
    }

    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);
    wl_surface_add_listener(surface, &s_listener, NULL);

    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    struct wl_toplevel_ctx ctx = {.surface = surface, .xs = xs, .w = 200, .h = 150,
                                  .color = 0xFF40C080u};
    xdg_surface_add_listener(xs, &wl_tl_xdg_listener, &ctx);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &wl_tl_listener, &ctx);
    xdg_toplevel_set_title(tl, "compositor6");
    xdg_toplevel_set_app_id(tl, "compositor6");
    wl_surface_commit(surface);

    for (int i = 0; i < 100 && !(got_scale && got_transform); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }

    if (!got_scale || pref_scale < 1) {
        fprintf(stderr, "no valid preferred_buffer_scale (got=%d val=%d)\n", got_scale, pref_scale);
        return 1;
    }
    if (!got_transform || pref_transform != WL_OUTPUT_TRANSFORM_NORMAL) {
        fprintf(stderr, "no valid preferred_buffer_transform (got=%d val=%u)\n",
                got_transform, pref_transform);
        return 1;
    }

    // v5 offset request must be accepted; a subsequent roundtrip stays alive
    wl_surface_offset(surface, 0, 0);
    wl_surface_commit(surface);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "offset request killed the connection\n");
        return 1;
    }

    printf("client_reg_compositor_version_6: scale %d transform %u\n", pref_scale, pref_transform);
    return 0;
}
