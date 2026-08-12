// Regression: source-less DnD is a valid same-client drag. Releasing the
// initiating button must deliver wl_data_device.drop without dereferencing a
// missing wl_data_source in the compositor.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_device* dev;
static int started;
static int entered;
static int dropped;

static void dev_data_offer(void* data, struct wl_data_device* device,
                           struct wl_data_offer* offer) {
    (void)data; (void)device; (void)offer;
}

static void dev_enter(void* data, struct wl_data_device* device, uint32_t serial,
                      struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                      struct wl_data_offer* offer) {
    (void)data; (void)device; (void)serial; (void)surface; (void)x; (void)y;

    if (offer) {
        fprintf(stderr, "source-less drag unexpectedly created an offer\n");
        exit(1);
    }

    entered = 1;
    printf("null-source entered\n");
}

static void dev_leave(void* data, struct wl_data_device* device) {
    (void)data; (void)device;
}

static void dev_motion(void* data, struct wl_data_device* device, uint32_t time,
                       wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)device; (void)time; (void)x; (void)y;
}

static void dev_drop(void* data, struct wl_data_device* device) {
    (void)data; (void)device;
    dropped = 1;
    printf("null-source dropped\n");
}

static void dev_selection(void* data, struct wl_data_device* device,
                          struct wl_data_offer* offer) {
    (void)data; (void)device; (void)offer;
}

static const struct wl_data_device_listener dev_listener = {
    dev_data_offer, dev_enter, dev_leave, dev_motion, dev_drop, dev_selection,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;
    if (!wl_ddm || !wl_seat_g || !wl_ptr) {
        fprintf(stderr, "missing globals\n");
        return 1;
    }

    wl_make_toplevel(&top, "dnd-null-source", 400, 300, 0xFFFF0000);
    dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &dev_listener, NULL);
    printf("null-source ready\n");

    for (int i = 0; i < 500; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;

        if (!started && wlp_button_count > 0 &&
            wlp_button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
            wl_data_device_start_drag(dev, NULL, top.surface, NULL, wlp_button_serial);
            started = 1;
            printf("null-source started\n");
        }

        if (dropped) {
            if (!entered) {
                fprintf(stderr, "drop arrived without enter\n");
                return 1;
            }

            printf("null-source ok\n");
            return 0;
        }

        usleep(20000);
    }

    fprintf(stderr, "null-source incomplete: started=%d entered=%d dropped=%d\n",
            started, entered, dropped);
    return 1;
}
