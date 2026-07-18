#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_device* dev;
static struct wl_data_offer* offer;
static const char* phase;
static int done;

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m; close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) {
    (void)d; (void)s; printf("source cancelled\n"); done = 1;
}
static void src_drop(void* d, struct wl_data_source* s) {
    (void)d; (void)s; printf("source drop\n");
}
static void src_finished(void* d, struct wl_data_source* s) {
    (void)d; (void)s; printf("source finish\n");
}
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_drop, src_finished, src_action,
};

static void offer_mime(void* d, struct wl_data_offer* o, const char* m) { (void)d; (void)o; (void)m; }
static void offer_actions(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static void offer_action(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static const struct wl_data_offer_listener offer_listener = {
    offer_mime, offer_actions, offer_action,
};

static void dev_offer(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd;
    offer = o;
    wl_data_offer_add_listener(o, &offer_listener, NULL);
}
static void dev_enter(void* d, struct wl_data_device* dd, uint32_t serial,
                      struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                      struct wl_data_offer* o) {
    (void)d; (void)dd; (void)surface; (void)x; (void)y;
    offer = o;
    if (o) {
        wl_data_offer_accept(o, serial, "text/plain");
        wl_data_offer_set_actions(o, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    }
    printf("target enter\n");
}
static void dev_leave(void* d, struct wl_data_device* dd) {
    (void)d; (void)dd; printf("target leave\n");
}
static void dev_motion(void* d, struct wl_data_device* dd, uint32_t t,
                       wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dd; (void)t; (void)x; (void)y;
    printf("target motion\n");
}
static void dev_drop(void* d, struct wl_data_device* dd) {
    (void)d; (void)dd;
    printf("target drop\n");
    if (!strcmp(phase, "finish") && offer) {
        wl_data_offer_finish(offer);
        wl_display_flush(wl_dpy);
        printf("target finish\n");
    }
}
static void dev_selection(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)o;
}
static const struct wl_data_device_listener dev_listener = {
    dev_offer, dev_enter, dev_leave, dev_motion, dev_drop, dev_selection,
};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (argc < 3 || wl_boot() || !wl_ddm || !wl_ptr) return 1;
    const char* role = argv[1];
    phase = argv[2];
    dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);

    if (!strcmp(role, "target")) {
        wl_data_device_add_listener(dev, &dev_listener, NULL);
        wl_make_toplevel(&top, "kill-target", 220, 150, 0xff00ff00);
        printf("target ready\n");
        while (wl_display_dispatch(wl_dpy) != -1) {
        }
        return 0;
    }

    wl_make_toplevel(&top, "kill-source", 220, 150, 0xffff0000);
    printf("source ready\n");
    while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
    }
    struct wl_data_source* src =
        wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    wl_data_source_offer(src, "text/plain");
    wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    wl_data_device_start_drag(dev, src, top.surface, NULL, wlp_button_serial);
    wl_display_flush(wl_dpy);
    printf("dragging\n");
    while (!done && wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
