// Regression: drag-and-drop interrupted mid-operation. Modes:
//   destroy-source — the source resource dies while the drag is active; the
//                    compositor must clean the drag up and keep routing input
//   no-accept     — the target never accepts a mime; releasing must CANCEL
//                   the source, not deliver a drop

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_toplevel_ctx top;
static struct wl_data_device* dev;
static struct wl_data_source* src;
static int entered, dropped, cancelled, drop_performed;

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; cancelled = 1; }
static void src_dnd_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; drop_performed = 1; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action,
};

// a data-device listener that deliberately never accepts the offer
static void dev_data_offer(void* d, struct wl_data_device* dd, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)o;
}
static void dev_enter(void* d, struct wl_data_device* dd, uint32_t serial, struct wl_surface* su,
                      wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* o) {
    (void)d; (void)dd; (void)serial; (void)su; (void)x; (void)y; (void)o;
    entered = 1;
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
    const char* mode = argc > 1 ? argv[1] : "destroy-source";
    alarm(30);
    if (wl_boot()) return 1;
    if (!wl_ddm) { fprintf(stderr, "no data device manager\n"); return 1; }

    wl_make_toplevel(&top, "dndphase", 300, 200, 0xFFFF0000);
    dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dev, &dev_listener, NULL);
    printf("ready\n");

    while (wlp_button_count == 0 && wl_display_dispatch(wl_dpy) != -1) {
    }

    src = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    wl_data_source_offer(src, "text/plain");
    wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    wl_data_device_start_drag(dev, src, top.surface, NULL, wlp_button_serial);
    wl_display_flush(wl_dpy);
    printf("dragging\n");

    if (!strcmp(mode, "destroy-source")) {
        wlk_watch_key = KEY_1;
        while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
        }
        wl_data_source_destroy(src);
        wl_display_roundtrip(wl_dpy);
        printf("source destroyed\n");

        // input must still reach us: the scenario clicks after the release
        int base = wlp_button_count;
        while (wlp_button_count < base + 2 && wl_display_dispatch(wl_dpy) != -1) {
        }
        printf("input alive\n");
        return 0;
    }

    // no-accept: wait for our own enter, then the scenario releases; the
    // source must be cancelled, never dropped
    while (!entered && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("entered\n");
    while (!cancelled && !dropped && !drop_performed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (dropped || drop_performed) {
        fprintf(stderr, "unaccepted drag was dropped (dropped=%d performed=%d)\n",
                dropped, drop_performed);
        return 1;
    }
    printf("cancelled ok\n");
    return 0;
}
