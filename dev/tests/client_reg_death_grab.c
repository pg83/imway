// Regression: a client dying at the worst moment. Modes:
//   grab  — red toplevel, opens a grab popup on the first click, then hangs
//           until the scenario kill -9s it mid-grab
//   drag  — red toplevel, starts a drag on the first click, then hangs with
//           the drag active until killed
//   check — green toplevel that expects a pointer click to still arrive
//           after the death; exits 0 on press+release, dies by alarm if the
//           compositor's input is stuck
// The compositor must survive both deaths and keep routing input.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_surface* popup_surface;
static struct xdg_surface* popup_xs;
static struct xdg_popup* popup;
static int popup_committed;

static void popup_xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!popup_committed) {
        wl_surface_attach(popup_surface, wl_solid(120, 90, 0xFFFFFF00), 0, 0);
        wl_surface_damage(popup_surface, 0, 0, 120, 90);
        wl_surface_commit(popup_surface);
        popup_committed = 1;
        printf("grabbed\n");
    }
}
static const struct xdg_surface_listener popup_xs_listener = {popup_xs_configure};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w,
                            int32_t h) {
    (void)d; (void)p; (void)x; (void)y; (void)w; (void)h;
}
static void popup_done(void* d, struct xdg_popup* p) {
    (void)d; (void)p;
    printf("popup done\n");
}
static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t t) {
    (void)d; (void)p; (void)t;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done,
                                                         popup_repositioned};

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action,
};

static void wait_first_press(void) {
    while (wlp_button_count == 0 && wl_display_dispatch(wl_dpy) != -1) {
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char* mode = argc > 1 ? argv[1] : "grab";
    alarm(30); // a stuck client must not outlive the scenario
    if (wl_boot()) return 1;

    if (!strcmp(mode, "check")) {
        wl_make_toplevel(&top, "checker", 300, 200, 0xFF00FF00);
        printf("checker ready\n");
        while (wlp_button_count < 2 && wl_display_dispatch(wl_dpy) != -1) {
        }
        printf("checker got click\n");
        return 0;
    }

    wl_make_toplevel(&top, mode, 300, 200, 0xFFFF0000);
    printf("ready\n");
    wait_first_press();

    if (!strcmp(mode, "grab")) {
        struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
        xdg_positioner_set_size(pos, 120, 90);
        xdg_positioner_set_anchor_rect(pos, 20, 20, 60, 20);
        xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
        xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);

        popup_surface = wl_compositor_create_surface(wl_comp);
        popup_xs = xdg_wm_base_get_xdg_surface(wl_wm, popup_surface);
        xdg_surface_add_listener(popup_xs, &popup_xs_listener, NULL);
        popup = xdg_surface_get_popup(popup_xs, top.xs, pos);
        xdg_popup_add_listener(popup, &popup_listener, NULL);
        xdg_popup_grab(popup, wl_seat_g, wlp_button_serial);
        wl_surface_commit(popup_surface);
        xdg_positioner_destroy(pos);
    } else { // drag
        if (!wl_ddm) { fprintf(stderr, "no data device manager\n"); return 1; }
        struct wl_data_device* dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
        (void)dev;
        struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
        wl_data_source_add_listener(src, &src_listener, NULL);
        wl_data_source_offer(src, "text/plain");
        wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        wl_data_device_start_drag(dev, src, top.surface, NULL, wlp_button_serial);
        printf("dragging\n");
    }

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
