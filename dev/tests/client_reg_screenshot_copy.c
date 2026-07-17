// End-to-end receiver for the screenshot cropper's Copy action. It validates
// the image/png payload, then replaces the selection so the hidden cropper is
// cancelled and can exit.

#include "wl_util.h"

static struct wl_toplevel_ctx top;
static struct wl_data_device* device;
static struct wl_data_offer* selection;
static int image_png;

static void offer_mime(void* data, struct wl_data_offer* offer, const char* mime) {
    (void)data; (void)offer;
    if (!strcmp(mime, "image/png")) image_png = 1;
}

static void offer_source_actions(void* data, struct wl_data_offer* offer, uint32_t actions) {
    (void)data; (void)offer; (void)actions;
}

static void offer_action(void* data, struct wl_data_offer* offer, uint32_t action) {
    (void)data; (void)offer; (void)action;
}

static const struct wl_data_offer_listener offer_listener = {
    offer_mime, offer_source_actions, offer_action,
};

static void device_offer(void* data, struct wl_data_device* dev, struct wl_data_offer* offer) {
    (void)data; (void)dev;
    wl_data_offer_add_listener(offer, &offer_listener, NULL);
}

static void device_enter(void* data, struct wl_data_device* dev, uint32_t serial,
                         struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                         struct wl_data_offer* offer) {
    (void)data; (void)dev; (void)serial; (void)surface; (void)x; (void)y; (void)offer;
}

static void device_leave(void* data, struct wl_data_device* dev) {
    (void)data; (void)dev;
}

static void device_motion(void* data, struct wl_data_device* dev, uint32_t time,
                          wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)dev; (void)time; (void)x; (void)y;
}

static void device_drop(void* data, struct wl_data_device* dev) {
    (void)data; (void)dev;
}

static void device_selection(void* data, struct wl_data_device* dev,
                             struct wl_data_offer* offer) {
    (void)data; (void)dev;
    selection = offer;
}

static const struct wl_data_device_listener device_listener = {
    device_offer, device_enter, device_leave, device_motion, device_drop,
    device_selection,
};

static void source_target(void* data, struct wl_data_source* source, const char* mime) {
    (void)data; (void)source; (void)mime;
}

static void source_send(void* data, struct wl_data_source* source, const char* mime, int32_t fd) {
    (void)data; (void)source; (void)mime;
    close(fd);
}

static void source_cancelled(void* data, struct wl_data_source* source) {
    (void)data; (void)source;
}

static void source_drop(void* data, struct wl_data_source* source) {
    (void)data; (void)source;
}

static void source_finished(void* data, struct wl_data_source* source) {
    (void)data; (void)source;
}

static void source_action(void* data, struct wl_data_source* source, uint32_t action) {
    (void)data; (void)source; (void)action;
}

static const struct wl_data_source_listener source_listener = {
    source_target, source_send, source_cancelled, source_drop, source_finished,
    source_action,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    if (wl_boot()) return 1;
    if (!wl_ddm || !wl_seat_g || !wl_kbd) {
        fprintf(stderr, "missing clipboard globals\n");
        return 1;
    }

    wl_make_toplevel(&top, "screenshot-copy-receiver", 320, 180, 0xFF00FF00);
    device = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(device, &device_listener, NULL);
    printf("copy receiver ready\n");

    for (int i = 0; i < 1000 && (!selection || !image_png); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(10000);
    }

    if (!selection || !image_png) {
        fprintf(stderr, "no image/png screenshot selection\n");
        return 1;
    }

    int fds[2];
    if (pipe(fds) < 0) {
        perror("pipe");
        return 1;
    }

    wl_data_offer_receive(selection, "image/png", fds[1]);
    close(fds[1]);
    wl_display_flush(wl_dpy);

    unsigned char signature[8] = {};
    size_t used = 0;

    while (used < sizeof(signature)) {
        wl_display_roundtrip(wl_dpy);
        ssize_t n = read(fds[0], signature + used, sizeof(signature) - used);
        if (n <= 0) break;
        used += (size_t)n;
    }

    close(fds[0]);

    static const unsigned char png_signature[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
    };

    if (used != sizeof(signature) || memcmp(signature, png_signature, sizeof(signature))) {
        fprintf(stderr, "screenshot clipboard payload is not PNG\n");
        return 1;
    }

    for (int i = 0; i < 300 && wlk_focus != top.surface; i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(10000);
    }

    if (wlk_focus != top.surface || !wlk_enter_serial) {
        fprintf(stderr, "receiver did not regain keyboard focus\n");
        return 1;
    }

    struct wl_data_source* replacement = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(replacement, &source_listener, NULL);
    wl_data_source_offer(replacement, "text/plain");
    wl_data_device_set_selection(device, replacement, wlk_enter_serial);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    printf("screenshot png ok\n");
    return 0;
}
