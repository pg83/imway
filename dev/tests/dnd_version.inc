#define REG_DDM_VERSION DND_VERSION
#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_offer* offer;
static int entered, dropped, source_actions, offer_action;
static int source_drop, source_finished, source_action;

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m; close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_drop_cb(void* d, struct wl_data_source* s) { (void)d; (void)s; source_drop++; }
static void src_finished_cb(void* d, struct wl_data_source* s) { (void)d; (void)s; source_finished++; }
static void src_action_cb(void* d, struct wl_data_source* s, uint32_t a) {
    (void)d; (void)s; (void)a; source_action++;
}
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_drop_cb, src_finished_cb, src_action_cb,
};
static void off_mime(void* d, struct wl_data_offer* o, const char* m) { (void)d; (void)o; (void)m; }
static void off_source_actions(void* d, struct wl_data_offer* o, uint32_t a) {
    (void)d; (void)o; (void)a; source_actions++;
}
static void off_action(void* d, struct wl_data_offer* o, uint32_t a) {
    (void)d; (void)o; (void)a; offer_action++;
}
static const struct wl_data_offer_listener off_listener = {
    off_mime, off_source_actions, off_action,
};
static void dev_offer(void* d, struct wl_data_device* dev, struct wl_data_offer* o) {
    (void)d; (void)dev; offer = o; wl_data_offer_add_listener(o, &off_listener, NULL);
}
static void dev_enter(void* d, struct wl_data_device* dev, uint32_t serial,
                      struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                      struct wl_data_offer* o) {
    (void)d; (void)dev; (void)surface; (void)x; (void)y;
    offer = o; entered = 1;
    wl_data_offer_accept(o, serial, "text/plain");
#if DND_VERSION >= 3
    wl_data_offer_set_actions(o, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                              WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
#endif
    printf("entered\n");
}
static void dev_leave(void* d, struct wl_data_device* dev) { (void)d; (void)dev; }
static void dev_motion(void* d, struct wl_data_device* dev, uint32_t t,
                       wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dev; (void)t; (void)x; (void)y;
}
static void dev_drop(void* d, struct wl_data_device* dev) {
    (void)d; (void)dev; dropped = 1;
#if DND_VERSION >= 3
    wl_data_offer_finish(offer);
#endif
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
    if (wl_boot() || !wl_ddm || !wl_ptr ||
        wl_data_device_manager_get_version(wl_ddm) != DND_VERSION) return 1;
    wl_make_toplevel(&top, "dnd-version", 240, 160, 0xffff0000);
    struct wl_data_device* dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &dev_listener, NULL);
    printf("ready\n");
    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {}
    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    wl_data_source_offer(src, "text/plain");
#if DND_VERSION >= 3
    wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
#endif
    wl_data_device_start_drag(dev, src, top.surface, NULL, wlp_button_serial);
    wl_display_flush(wl_dpy);
    printf("dragging\n");
    while ((!dropped || (DND_VERSION >= 3 && !source_finished)) &&
           wl_display_dispatch(wl_dpy) != -1) {}

    printf("dnd-v%d source_actions=%d offer_action=%d drop=%d finished=%d source_action=%d\n",
           DND_VERSION, source_actions, offer_action, source_drop, source_finished, source_action);
#if DND_VERSION >= 3
    return entered && dropped && source_actions && offer_action && source_drop &&
           source_finished && source_action ? 0 : 1;
#else
    return entered && dropped && !source_actions && !offer_action && !source_drop &&
           !source_finished && !source_action ? 0 : 1;
#endif
}
