// Regression (#15): wl_data_source.cancelled is a v1 event, but the
// compositor gated it behind version >= 3, so a data-device v1/v2 client
// whose drag was rejected never learned the source died. Repro: bind
// data_device_manager at v1 and start_drag with an invalid serial (no button
// is held, so the compositor rejects it) — the client must still get
// cancelled.

#define REG_DDM_VERSION 1
#include "wl_util.h"

static struct wl_toplevel_ctx top;
static int cancelled;

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m; close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) {
    (void)d; (void)s;
    cancelled = 1;
    printf("client_reg_dnd_cancel: cancelled\n");
}
static void src_dnd_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ddm || !wl_seat_g) {
        fprintf(stderr, "client_reg_dnd_cancel: no data-device manager / seat\n");
        return 1;
    }
    printf("client_reg_dnd_cancel: ddm version %d\n", wl_data_device_manager_get_version(wl_ddm));

    wl_make_toplevel(&top, "client_reg_dnd_cancel", 300, 200, 0xFFFF0000);

    struct wl_data_device* dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    wl_data_source_offer(src, "text/plain");

    // no button is held → the compositor rejects the drag and must cancel the
    // source; the bogus serial only needs to be non-matching
    wl_data_device_start_drag(dev, src, top.surface, NULL, 123456);

    for (int i = 0; i < 40 && !cancelled; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }

    return cancelled ? 0 : 1;
}
