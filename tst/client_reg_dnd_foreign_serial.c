#include "wl_util.h"

static int cancelled;
static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m; close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; cancelled = 1; }
static void src_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_drop, src_finished, src_action,
};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot() || !wl_ddm || !wl_ptr) return 1;

    struct wl_toplevel_ctx top;
    if (argc < 2 || !strcmp(argv[1], "owner")) {
        wl_make_toplevel(&top, "serial-owner", 220, 140, 0xffff0000);
        printf("owner ready\n");
        while (!wlp_button_count && wl_display_dispatch(wl_dpy) != -1) {
        }
        printf("foreign serial %u\n", wlp_button_serial);
        while (wl_display_dispatch(wl_dpy) != -1) {
        }
        return 0;
    }

    uint32_t serial = (uint32_t)strtoul(argv[1], NULL, 10);
    wl_make_toplevel(&top, "serial-attacker", 220, 140, 0xff00ff00);
    struct wl_data_device* dev =
        wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct wl_data_source* src =
        wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    wl_data_source_offer(src, "text/plain");
    wl_data_device_start_drag(dev, src, top.surface, NULL, serial);
    wl_display_flush(wl_dpy);
    while (!cancelled && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!cancelled) return 1;
    printf("foreign serial cancelled\n");
    return 0;
}
