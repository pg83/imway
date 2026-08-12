// Feature: wl_data_device clipboard round-trip. A focused client sets a
// selection, the compositor offers it back to that client's data_device, and
// receive() pipes the payload from the source. Exercises set_selection ->
// data_offer -> selection -> receive -> source.send end to end.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_device* dev;
static struct wl_data_offer* current_offer;
static int selection_offered;

static const char* PAYLOAD = "imway-clip";

// --- source ---
static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    ssize_t r = write(fd, PAYLOAD, strlen(PAYLOAD));
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

// --- offer / device ---
static void offer_offer(void* d, struct wl_data_offer* o, const char* mime) {
    (void)d; (void)o; (void)mime;
}
static void offer_src_actions(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static void offer_action(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static const struct wl_data_offer_listener offer_listener = {offer_offer, offer_src_actions, offer_action};

static void dev_data_offer(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd;
    wl_data_offer_add_listener(o, &offer_listener, NULL);
}
static void dev_enter(void* d, struct wl_data_device* dd, uint32_t s, struct wl_surface* su,
                      wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)s; (void)su; (void)x; (void)y; (void)o;
}
static void dev_leave(void* d, struct wl_data_device* dd) { (void)d; (void)dd; }
static void dev_motion(void* d, struct wl_data_device* dd, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dd; (void)t; (void)x; (void)y;
}
static void dev_drop(void* d, struct wl_data_device* dd) { (void)d; (void)dd; }
static void dev_selection(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd;
    if (o) { current_offer = o; selection_offered = 1; }
}
static const struct wl_data_device_listener dev_listener = {
    dev_data_offer, dev_enter, dev_leave, dev_motion, dev_drop, dev_selection,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ddm || !wl_seat_g || !wl_kbd) { fprintf(stderr, "missing globals\n"); return 1; }

    wl_make_toplevel(&top, "client_feat_selection", 400, 300, 0xFFFF0000);

    for (int i = 0; i < 200 && !wlk_key_serial; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!wlk_key_serial) { fprintf(stderr, "no key serial\n"); return 1; }

    dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &dev_listener, NULL);

    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    wl_data_source_offer(src, "text/plain");
    wl_data_device_set_selection(dev, src, wlk_key_serial);

    for (int i = 0; i < 100 && !selection_offered; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!selection_offered) { fprintf(stderr, "no selection offer\n"); return 1; }

    int fds[2];
    if (pipe(fds) < 0) { perror("pipe"); return 1; }
    wl_data_offer_receive(current_offer, "text/plain", fds[1]);
    close(fds[1]);
    wl_display_flush(wl_dpy);

    char buf[64] = {0};
    ssize_t total = 0, n;
    for (int i = 0; i < 100; i++) {
        wl_display_roundtrip(wl_dpy);
        while ((n = read(fds[0], buf + total, sizeof(buf) - 1 - total)) > 0) total += n;
        if (total >= (ssize_t)strlen(PAYLOAD)) break;
        usleep(20000);
    }
    close(fds[0]);

    printf("client_feat_selection: read '%s'\n", buf);
    if (strcmp(buf, PAYLOAD) != 0) { fprintf(stderr, "payload mismatch\n"); return 1; }
    printf("client_feat_selection: ok\n");
    return 0;
}
