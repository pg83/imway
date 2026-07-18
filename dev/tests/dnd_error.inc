#include "wl_util.h"

static struct wl_toplevel_ctx dnd_top;
static struct wl_data_device* dnd_device;
static struct wl_data_source* dnd_source;
static struct wl_data_offer* dnd_offer;

static void offer_offer(void* d, struct wl_data_offer* offer, const char* mime) {
    (void)d; (void)offer; (void)mime;
}
static void offer_source_actions(void* d, struct wl_data_offer* offer, uint32_t actions) {
    (void)d; (void)offer; (void)actions;
}
static void offer_action(void* d, struct wl_data_offer* offer, uint32_t action) {
    (void)d; (void)offer; (void)action;
}
static const struct wl_data_offer_listener offer_listener = {
    offer_offer, offer_source_actions, offer_action,
};

static void device_data_offer(void* d, struct wl_data_device* device,
                              struct wl_data_offer* offer) {
    (void)d; (void)device;
    dnd_offer = offer;
    wl_data_offer_add_listener(offer, &offer_listener, NULL);
}
static void device_enter(void* d, struct wl_data_device* device, uint32_t serial,
                         struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                         struct wl_data_offer* offer) {
    (void)d; (void)device; (void)serial; (void)surface; (void)x; (void)y; (void)offer;
}
static void device_leave(void* d, struct wl_data_device* device) { (void)d; (void)device; }
static void device_motion(void* d, struct wl_data_device* device, uint32_t time,
                          wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)device; (void)time; (void)x; (void)y;
}
static void device_drop(void* d, struct wl_data_device* device) { (void)d; (void)device; }
static void device_selection(void* d, struct wl_data_device* device,
                             struct wl_data_offer* offer) {
    (void)d; (void)device; (void)offer;
}
static const struct wl_data_device_listener device_listener = {
    device_data_offer, device_enter, device_leave, device_motion, device_drop,
    device_selection,
};

static void source_target(void* d, struct wl_data_source* source, const char* mime) {
    (void)d; (void)source; (void)mime;
}
static void source_send(void* d, struct wl_data_source* source, const char* mime, int32_t fd) {
    (void)d; (void)source; (void)mime;
    close(fd);
}
static void source_cancelled(void* d, struct wl_data_source* source) {
    (void)d; (void)source;
}
static void source_drop(void* d, struct wl_data_source* source) { (void)d; (void)source; }
static void source_finished(void* d, struct wl_data_source* source) { (void)d; (void)source; }
static void source_action(void* d, struct wl_data_source* source, uint32_t action) {
    (void)d; (void)source; (void)action;
}
static const struct wl_data_source_listener source_listener = {
    source_target, source_send, source_cancelled, source_drop, source_finished, source_action,
};

static int dnd_error_boot(void) {
    if (wl_boot() || !wl_ddm || !wl_ptr) return 2;
    wl_make_toplevel(&dnd_top, "dnd-error", 240, 160, 0xffff0000);
    dnd_device = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dnd_device, &device_listener, NULL);
    printf("mapped\n");
    return 0;
}

static void dnd_error_start(struct wl_surface* icon) {
    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
    }
    dnd_source = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(dnd_source, &source_listener, NULL);
    wl_data_source_offer(dnd_source, "text/plain");
    wl_data_source_set_actions(dnd_source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    printf("dragging\n");
    wl_data_device_start_drag(dnd_device, dnd_source, dnd_top.surface, icon,
                              wlp_button_serial);
    wl_display_flush(wl_dpy);
    while (!dnd_offer && wl_display_dispatch(wl_dpy) != -1) {
    }
}
