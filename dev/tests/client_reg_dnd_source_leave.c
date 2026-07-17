// Regression (#16): when a drag data source dies mid-drag, the drag target
// must receive wl_data_device.leave, or it stays stuck in an active-DnD
// state forever. Repro: start a real drag over our own surface (so it becomes
// the drag target and gets enter), then destroy the source and expect leave.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_device* dev;
static struct wl_data_source* src;
static int entered, left, drag_started;

static void offer_offer(void* d, struct wl_data_offer* o, const char* m) { (void)d; (void)o; (void)m; }
static void offer_actions(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static void offer_action(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static const struct wl_data_offer_listener offer_listener = {offer_offer, offer_actions, offer_action};

static void dev_data_offer(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd;
    wl_data_offer_add_listener(o, &offer_listener, NULL);
}
static void dev_enter(void* d, struct wl_data_device* dd, uint32_t s, struct wl_surface* su,
                      wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)s; (void)su; (void)x; (void)y; (void)o;
    entered = 1;
}
static void dev_leave(void* d, struct wl_data_device* dd) {
    (void)d; (void)dd;
    left = 1;
}
static void dev_motion(void* d, struct wl_data_device* dd, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dd; (void)t; (void)x; (void)y;
}
static void dev_drop(void* d, struct wl_data_device* dd) { (void)d; (void)dd; }
static void dev_selection(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)o;
}
static const struct wl_data_device_listener dev_listener = {
    dev_data_offer, dev_enter, dev_leave, dev_motion, dev_drop, dev_selection,
};

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m; close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ddm || !wl_seat_g || !wl_ptr) {
        fprintf(stderr, "client_reg_dnd_source_leave: missing globals\n");
        return 1;
    }

    wl_make_toplevel(&top, "client_reg_dnd_source_leave", 400, 300, 0xFFFF0000);
    printf("client_reg_dnd_source_leave: mapped\n");

    dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &dev_listener, NULL);

    for (int i = 0; i < 400; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;

        // the scenario points at the window and holds the button; that serial
        // starts a real drag with our own surface as origin
        if (!drag_started && wlp_button_count > 0 &&
            wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
            src = wl_data_device_manager_create_data_source(wl_ddm);
            wl_data_source_add_listener(src, &src_listener, NULL);
            wl_data_source_offer(src, "text/plain");
            wl_data_device_start_drag(dev, src, top.surface, NULL, wlp_button_serial);
            drag_started = 1;
            printf("client_reg_dnd_source_leave: drag started\n");
        }

        // once the drag target (us) has been entered, kill the source
        if (drag_started && entered && src) {
            wl_data_source_destroy(src);
            src = NULL;
            printf("client_reg_dnd_source_leave: source destroyed\n");
        }

        if (left) {
            printf("client_reg_dnd_source_leave: target got leave\n");
            return 0;
        }

        usleep(20000);
    }

    fprintf(stderr, "client_reg_dnd_source_leave: no leave (entered=%d)\n", entered);
    return 1;
}
