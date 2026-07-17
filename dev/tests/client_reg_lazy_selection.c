// Regression (#25): a data_device bound while its client already holds
// keyboard focus (a terminal binding on the first Ctrl+V) never received the
// current selection until the next focus change. get_data_device must send it
// at bind time. Repro: take focus, set a clipboard selection, then bind a
// second data_device and expect it to be handed the selection immediately.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static int late_got_selection;

// --- offer / device listeners (only the late device needs to observe) ---

static void offer_offer(void* d, struct wl_data_offer* o, const char* mime) {
    (void)d; (void)o; (void)mime;
}
static void offer_source_actions(void* d, struct wl_data_offer* o, uint32_t a) {
    (void)d; (void)o; (void)a;
}
static void offer_action(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static const struct wl_data_offer_listener offer_listener = {
    offer_offer, offer_source_actions, offer_action,
};

static void dev_data_offer(void* d, struct wl_data_device* dev, struct wl_data_offer* o) {
    (void)d; (void)dev;
    wl_data_offer_add_listener(o, &offer_listener, NULL);
}
static void dev_enter(void* d, struct wl_data_device* dev, uint32_t s, struct wl_surface* su,
                      wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* o) {
    (void)d; (void)dev; (void)s; (void)su; (void)x; (void)y; (void)o;
}
static void dev_leave(void* d, struct wl_data_device* dev) { (void)d; (void)dev; }
static void dev_motion(void* d, struct wl_data_device* dev, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dev; (void)t; (void)x; (void)y;
}
static void dev_drop(void* d, struct wl_data_device* dev) { (void)d; (void)dev; }
static void dev_selection(void* d, struct wl_data_device* dev, struct wl_data_offer* o) {
    (void)dev;
    int* got = d;
    if (o) *got = 1;
}
static const struct wl_data_device_listener dev_listener = {
    dev_data_offer, dev_enter, dev_leave, dev_motion, dev_drop, dev_selection,
};

// --- source we put on the clipboard ---

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    const char* payload = "hello";
    ssize_t r = write(fd, payload, 5);
    (void)r;
    close(fd);
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
    if (!wl_ddm || !wl_seat_g || !wl_kbd) {
        fprintf(stderr, "client_reg_lazy_selection: missing globals\n");
        return 1;
    }

    wl_make_toplevel(&top, "client_reg_lazy_selection", 400, 300, 0xFFFF0000);

    // wait for keyboard focus and a key serial (the scenario injects a key)
    for (int i = 0; i < 200 && !wlk_key_serial; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!wlk_key_serial) {
        fprintf(stderr, "client_reg_lazy_selection: no key serial\n");
        return 1;
    }

    // an early device sets the selection
    struct wl_data_device* dev1 = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    wl_data_source_offer(src, "text/plain");
    wl_data_device_set_selection(dev1, src, wlk_key_serial);
    wl_display_roundtrip(wl_dpy);

    printf("client_reg_lazy_selection: selection set, binding late device\n");

    // the late device must be handed the current selection at bind time
    struct wl_data_device* dev2 = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev2, &dev_listener, &late_got_selection);

    for (int i = 0; i < 40 && !late_got_selection; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }

    if (!late_got_selection) {
        fprintf(stderr, "client_reg_lazy_selection: late device got no selection\n");
        return 1;
    }
    printf("client_reg_lazy_selection: late device got selection\n");
    return 0;
}
