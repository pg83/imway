// The drag target sets its actions with no preferred one, so the compositor
// picks from what both sides offer: copy over move, and none when they share
// nothing. The includer defines SRC_ACTIONS, OFFER_ACTIONS and WANT_ACTION.
#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_offer* offer;
static int entered, dropped, cancelled, source_finished;
static uint32_t offer_action = 0xffffffff;

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m; close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; cancelled = 1; }
static void src_drop_cb(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_finished_cb(void* d, struct wl_data_source* s) { (void)d; (void)s; source_finished = 1; }
static void src_action_cb(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_drop_cb, src_finished_cb, src_action_cb,
};
static void off_mime(void* d, struct wl_data_offer* o, const char* m) { (void)d; (void)o; (void)m; }
static void off_source_actions(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static void off_action(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; offer_action = a; }
static const struct wl_data_offer_listener off_listener = {off_mime, off_source_actions, off_action};

static void dev_offer(void* d, struct wl_data_device* dev, struct wl_data_offer* o) {
    (void)d; (void)dev; offer = o; wl_data_offer_add_listener(o, &off_listener, NULL);
}
static void dev_enter(void* d, struct wl_data_device* dev, uint32_t serial,
                      struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                      struct wl_data_offer* o) {
    (void)d; (void)dev; (void)surface; (void)x; (void)y;
    offer = o; entered = 1;
    wl_data_offer_accept(o, serial, "text/plain");
    wl_data_offer_set_actions(o, OFFER_ACTIONS, WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE);
    printf("entered\n");
}
static void dev_leave(void* d, struct wl_data_device* dev) { (void)d; (void)dev; }
static void dev_motion(void* d, struct wl_data_device* dev, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dev; (void)t; (void)x; (void)y;
}
static void dev_drop(void* d, struct wl_data_device* dev) {
    (void)d; (void)dev; dropped = 1;
    if (WANT_ACTION != WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) {
        wl_data_offer_finish(offer);
    }
}
static void dev_selection(void* d, struct wl_data_device* dev, struct wl_data_offer* o) {
    (void)d; (void)dev; (void)o;
}
static const struct wl_data_device_listener dev_listener = {
    dev_offer, dev_enter, dev_leave, dev_motion, dev_drop, dev_selection,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_ddm || !wl_ptr) return 1;
    wl_make_toplevel(&top, "dnd-choose", 240, 160, 0xffff0000);
    struct wl_data_device* dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &dev_listener, NULL);
    printf("ready\n");
    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {}
    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    wl_data_source_offer(src, "text/plain");
    wl_data_source_set_actions(src, SRC_ACTIONS);
    wl_data_device_start_drag(dev, src, top.surface, NULL, wlp_button_serial);
    wl_display_flush(wl_dpy);
    printf("dragging\n");
    if (WANT_ACTION == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) {
        // nothing in common: the drop cannot complete, the source is cancelled
        while (!cancelled && wl_display_dispatch(wl_dpy) != -1) {}
    } else {
        while ((!dropped || !source_finished) && wl_display_dispatch(wl_dpy) != -1) {}
    }
    printf("dnd-choose entered=%d action=%u want=%u\n", entered, offer_action, (unsigned)WANT_ACTION);
    return entered && offer_action == WANT_ACTION ? 0 : 1;
}
