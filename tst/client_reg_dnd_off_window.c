// A drag leaving every window and coming back: the drag target gets a leave
// when the pointer moves over the bare desktop and a fresh enter when it
// returns. A second button pressed and released mid-drag does not end the
// drag; the release of the button that started it does, and with nothing
// accepted the source is cancelled.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_device* dev;
static struct wl_data_source* src;
static int started, entered, left, dropped, cancelled;

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
    entered++;
    printf("entered %d\n", entered);
}
static void dev_leave(void* d, struct wl_data_device* dd) {
    (void)d; (void)dd;
    left++;
    printf("left %d\n", left);
}
static void dev_motion(void* d, struct wl_data_device* dd, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dd; (void)t; (void)x; (void)y;
}
static void dev_drop(void* d, struct wl_data_device* dd) {
    (void)d; (void)dd;
    dropped++;
}
static void dev_selection(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)o;
}
static const struct wl_data_device_listener dev_listener = {
    dev_data_offer, dev_enter, dev_leave, dev_motion, dev_drop, dev_selection,
};

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) {
    (void)d; (void)s;
    cancelled++;
    printf("source cancelled\n");
}
static void src_dnd_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) return 1;
    if (!wl_ddm || !wl_seat_g || !wl_ptr) {
        fprintf(stderr, "missing globals\n");
        return 1;
    }

    wl_make_toplevel(&top, "dnd-off-window", 400, 300, 0xFFFF0000);
    dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &dev_listener, NULL);
    printf("off-window ready\n");

    while (!cancelled) {
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "connection lost\n");
            return 1;
        }

        if (!started && wlp_button_count > 0 && wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
            src = wl_data_device_manager_create_data_source(wl_ddm);
            wl_data_source_add_listener(src, &src_listener, NULL);
            wl_data_source_offer(src, "text/plain");
            wl_data_device_start_drag(dev, src, top.surface, NULL, wlp_button_serial);
            started = 1;
            printf("drag started\n");
        }

        usleep(20000);
    }

    wl_display_roundtrip(wl_dpy);

    if (dropped) {
        fprintf(stderr, "a drag nobody accepted was dropped\n");
        return 1;
    }

    printf("off-window done entered=%d left=%d\n", entered, left);
    wl_data_source_destroy(src);
    return 0;
}
