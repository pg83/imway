// Feature: full drag-and-drop. Start a real drag over our own surface (so it
// becomes the drop target), accept the offer, and on drop pipe the payload
// through. Exercises start_drag -> enter -> accept/set_actions -> drop ->
// receive -> source.send -> finish.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_device* dev;
static struct wl_data_source* src;
static struct wl_data_offer* offer;
static uint32_t enter_serial;
static int drag_started, entered, dropped, done;

static const char* PAYLOAD = "dnd-payload";

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    ssize_t r = write(fd, PAYLOAD, strlen(PAYLOAD));
    (void)r; close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action,
};

static void offer_offer(void* d, struct wl_data_offer* o, const char* m) { (void)d; (void)o; (void)m; }
static void offer_src_actions(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static void offer_action(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static const struct wl_data_offer_listener offer_listener = {offer_offer, offer_src_actions, offer_action};

static void dev_data_offer(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd;
    wl_data_offer_add_listener(o, &offer_listener, NULL);
}
static void dev_enter(void* d, struct wl_data_device* dd, uint32_t serial, struct wl_surface* su,
                      wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)su; (void)x; (void)y;
    offer = o;
    enter_serial = serial;
    entered = 1;
    if (o) {
        wl_data_offer_accept(o, serial, "text/plain");
        wl_data_offer_set_actions(o, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    }
}
static void dev_leave(void* d, struct wl_data_device* dd) { (void)d; (void)dd; }
static void dev_motion(void* d, struct wl_data_device* dd, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dd; (void)t; (void)x; (void)y;
}
static void dev_drop(void* d, struct wl_data_device* dd) { (void)d; (void)dd; dropped = 1; }
static void dev_selection(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)o;
}
static const struct wl_data_device_listener dev_listener = {
    dev_data_offer, dev_enter, dev_leave, dev_motion, dev_drop, dev_selection,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_ddm || !wl_seat_g || !wl_ptr) { fprintf(stderr, "missing globals\n"); return 1; }

    wl_make_toplevel(&top, "client_feat_dnd", 400, 300, 0xFFFF0000);
    printf("client_feat_dnd: mapped\n");

    dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &dev_listener, NULL);

    for (int i = 0; i < 500 && !done; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;

        if (!drag_started && wlp_button_count > 0 &&
            wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
            src = wl_data_device_manager_create_data_source(wl_ddm);
            wl_data_source_add_listener(src, &src_listener, NULL);
            wl_data_source_offer(src, "text/plain");
            wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
            wl_data_device_start_drag(dev, src, top.surface, NULL, wlp_button_serial);
            drag_started = 1;
            printf("client_feat_dnd: drag started\n");
        }

        if (dropped && offer && !done) {
            int fds[2];
            if (pipe(fds) < 0) { perror("pipe"); return 1; }
            wl_data_offer_receive(offer, "text/plain", fds[1]);
            close(fds[1]);
            wl_display_flush(wl_dpy);

            char buf[64] = {0};
            ssize_t total = 0, n;
            for (int j = 0; j < 100; j++) {
                wl_display_roundtrip(wl_dpy);
                while ((n = read(fds[0], buf + total, sizeof(buf) - 1 - total)) > 0) total += n;
                if (total >= (ssize_t)strlen(PAYLOAD)) break;
                usleep(20000);
            }
            close(fds[0]);
            wl_data_offer_finish(offer);
            wl_display_flush(wl_dpy);

            printf("client_feat_dnd: dropped, read '%s'\n", buf);
            done = 1;
            if (strcmp(buf, PAYLOAD) != 0) { fprintf(stderr, "payload mismatch\n"); return 1; }
            printf("client_feat_dnd: ok\n");
            return 0;
        }

        usleep(20000);
    }

    fprintf(stderr, "client_feat_dnd: incomplete entered=%d dropped=%d\n", entered, dropped);
    return 1;
}
