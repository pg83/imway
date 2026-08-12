#include "wl_util.h"

static struct wl_data_offer* selection_offer;

static void sel_offer(void* d, struct wl_data_offer* o, const char* mime) {
    (void)d; (void)o; (void)mime;
}
static void sel_source_actions(void* d, struct wl_data_offer* o, uint32_t actions) {
    (void)d; (void)o; (void)actions;
}
static void sel_action(void* d, struct wl_data_offer* o, uint32_t action) {
    (void)d; (void)o; (void)action;
}
static const struct wl_data_offer_listener sel_offer_listener = {
    sel_offer, sel_source_actions, sel_action,
};

static void sel_data_offer(void* d, struct wl_data_device* dev, struct wl_data_offer* offer) {
    (void)d; (void)dev;
    wl_data_offer_add_listener(offer, &sel_offer_listener, NULL);
}
static void sel_enter(void* d, struct wl_data_device* dev, uint32_t serial,
                      struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                      struct wl_data_offer* offer) {
    (void)d; (void)dev; (void)serial; (void)surface; (void)x; (void)y; (void)offer;
}
static void sel_leave(void* d, struct wl_data_device* dev) { (void)d; (void)dev; }
static void sel_motion(void* d, struct wl_data_device* dev, uint32_t time,
                       wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dev; (void)time; (void)x; (void)y;
}
static void sel_drop(void* d, struct wl_data_device* dev) { (void)d; (void)dev; }
static void sel_selection(void* d, struct wl_data_device* dev, struct wl_data_offer* offer) {
    (void)d; (void)dev;
    selection_offer = offer;
}
static const struct wl_data_device_listener sel_device_listener = {
    sel_data_offer, sel_enter, sel_leave, sel_motion, sel_drop, sel_selection,
};

static int selection_offer_boot(void) {
    if (wl_boot() || !wl_ddm || !wl_seat_g) return 2;

    static struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "selection-offer-error", 220, 140, 0xffff0000);
    if (!wlk_enter_serial) return 2;

    struct wl_data_device* dev =
        wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &sel_device_listener, NULL);
    struct wl_data_source* src =
        wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_offer(src, "text/plain");
    wl_data_device_set_selection(dev, src, wlk_enter_serial);

    while (!selection_offer && wl_display_dispatch(wl_dpy) != -1) {
    }
    return selection_offer ? 0 : 1;
}
