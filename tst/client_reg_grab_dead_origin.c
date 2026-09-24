// A press whose surface dies under the held button: a 220x150 red window
// carries a 100x100 blue subsurface, the scenario presses on the blue, and
// the client starts a drag from it (the pointer leaves every surface for
// the drag, the press stays held) and destroys the subsurface before
// asking for an interactive move of its window with the press's serial,
// then for a popup grab with it. The press can no longer be matched to a
// window: the move does nothing and the popup is dismissed at once.

#include "wl_util.h"

static int popup_done;

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_dismissed(void* d, struct xdg_popup* p) {
    (void)d; (void)p;
    popup_done = 1;
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d; (void)p; (void)token;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_dismissed, popup_repositioned};

static void popup_xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_ptr || !wl_subcomp || !wl_ddm) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "grab-dead-origin", 220, 150, 0xffff0000);

    struct wl_surface* cell = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, cell, top.surface);
    wl_subsurface_set_position(sub, 60, 25);
    wl_subsurface_set_desync(sub);
    wl_surface_attach(cell, wl_solid(100, 100, 0xff0000ff), 0, 0);
    wl_surface_damage(cell, 0, 0, 100, 100);
    wl_surface_commit(cell);
    wl_surface_commit(top.surface);
    wl_await_presented(top.surface);
    printf("dead origin ready\n");

    while (wlp_focus != cell && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("pointer on the subsurface\n");

    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (wlp_focus != cell) {
        fprintf(stderr, "the press did not land on the subsurface\n");
        return 1;
    }

    uint32_t serial = wlp_button_serial;
    struct wl_data_device* device = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct wl_data_source* source = wl_data_device_manager_create_data_source(wl_ddm);

    wl_data_source_offer(source, "text/plain");
    wl_data_device_start_drag(device, source, cell, NULL, serial);
    wl_display_roundtrip(wl_dpy);
    printf("drag started\n");

    wl_subsurface_destroy(sub);
    wl_surface_destroy(cell);
    xdg_toplevel_move(top.tl, wl_seat_g, serial);
    printf("dead-origin move requested\n");

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 40, 40);
    xdg_positioner_set_anchor_rect(pos, 0, 0, 10, 10);

    struct wl_surface* ps = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wl_wm, ps);
    xdg_surface_add_listener(pxs, &popup_xs_listener, NULL);

    struct xdg_popup* popup = xdg_surface_get_popup(pxs, top.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_popup_grab(popup, wl_seat_g, serial);
    wl_surface_commit(ps);

    while (!popup_done && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!popup_done) {
        fprintf(stderr, "the connection ended before the popup was dismissed\n");
        return 1;
    }
    printf("dead-origin popup dismissed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
