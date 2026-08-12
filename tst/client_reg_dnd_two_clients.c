// Regression: drag-and-drop between two SEPARATE clients (every other dnd
// test self-drops). Mode "target" maps green, accepts and receives; mode
// "source" maps red, starts the drag on the first press and serves the
// payload. The scenario drags from the red window onto the green one.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_device* dev;
static struct wl_data_offer* offer;
static int dropped, finished;

static const char* PAYLOAD = "cross-client-payload";

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    ssize_t r = write(fd, PAYLOAD, strlen(PAYLOAD));
    (void)r;
    close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) {
    (void)d; (void)s;
    fprintf(stderr, "source cancelled\n");
    exit(1);
}
static void src_dnd_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; finished = 1; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action,
};

static void dev_data_offer(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)o;
}
static void dev_enter(void* d, struct wl_data_device* dd, uint32_t serial, struct wl_surface* su,
                      wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)su; (void)x; (void)y;
    offer = o;
    if (o) {
        wl_data_offer_accept(o, serial, "text/plain");
        wl_data_offer_set_actions(o, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        printf("target entered\n");
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

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char* mode = argc > 1 ? argv[1] : "target";
    alarm(30);
    if (wl_boot()) return 1;
    if (!wl_ddm) { fprintf(stderr, "no data device manager\n"); return 1; }

    dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &dev_listener, NULL);

    if (!strcmp(mode, "source")) {
        wl_make_toplevel(&top, "dndsrc", 200, 150, 0xFFFF0000);
        printf("source ready\n");

        while (wlp_button_count == 0 && wl_display_dispatch(wl_dpy) != -1) {
        }
        struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
        wl_data_source_add_listener(src, &src_listener, NULL);
        wl_data_source_offer(src, "text/plain");
        wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        wl_data_device_start_drag(dev, src, top.surface, NULL, wlp_button_serial);
        wl_display_flush(wl_dpy);
        printf("dragging\n");

        while (!finished && wl_display_dispatch(wl_dpy) != -1) {
        }
        printf("source finished\n");
        return 0;
    }

    wl_make_toplevel(&top, "dndtgt", 200, 150, 0xFF00FF00);
    printf("target ready\n");

    while (!dropped && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!offer) { fprintf(stderr, "drop without an offer\n"); return 1; }

    int fds[2];
    if (pipe(fds) < 0) { perror("pipe"); return 1; }
    wl_data_offer_receive(offer, "text/plain", fds[1]);
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
    wl_data_offer_finish(offer);
    wl_display_flush(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (strcmp(buf, PAYLOAD) != 0) {
        fprintf(stderr, "payload mismatch: '%s'\n", buf);
        return 1;
    }
    printf("target received\n");
    return 0;
}
