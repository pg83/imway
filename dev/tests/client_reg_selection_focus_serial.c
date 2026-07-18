#include "wl_util.h"

static int cancelled;
static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m; close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; cancelled++; }
static void src_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_drop, src_finished, src_action,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(15);
    if (wl_boot() || !wl_ddm || !wl_kbd) return 1;

    struct wl_data_device* dev =
        wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct wl_toplevel_ctx first;
    wl_make_toplevel(&first, "selection-focus-first", 200, 130, 0xffff0000);
    uint32_t old_serial = wlk_enter_serial;
    if (!old_serial) return 1;

    struct wl_data_source* original =
        wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(original, &src_listener, NULL);
    wl_data_source_offer(original, "text/plain");
    wl_data_device_set_selection(dev, original, old_serial);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    struct wl_toplevel_ctx second;
    wl_make_toplevel(&second, "selection-focus-second", 200, 130, 0xff00ff00);
    if (wlk_focus != second.surface) return 1;

    struct wl_data_source* replay =
        wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_offer(replay, "text/plain");
    wl_data_device_set_selection(dev, replay, old_serial);
    if (wl_display_roundtrip(wl_dpy) < 0 ||
        wl_display_roundtrip(wl_dpy) < 0) return 1;

    printf("selection focus replay cancelled=%d\n", cancelled);
    return cancelled == 0 ? 0 : 1;
}
