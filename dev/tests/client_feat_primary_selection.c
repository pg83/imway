// Feature: primary selection (middle-click paste) round-trip. Same shape as
// the clipboard test but over zwp_primary_selection_*: a focused client sets
// the primary selection, gets it offered back, and pipes the payload.

#include "wl_util.h"
#include <primary-selection-unstable-v1-client-protocol.h>

static struct zwp_primary_selection_device_manager_v1* pmgr;
static struct wl_toplevel_ctx top;
static struct zwp_primary_selection_offer_v1* current_offer;
static int selection_offered;

static const char* PAYLOAD = "imway-primary";

static void src_send(void* d, struct zwp_primary_selection_source_v1* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    ssize_t r = write(fd, PAYLOAD, strlen(PAYLOAD));
    (void)r;
    close(fd);
}
static void src_cancelled(void* d, struct zwp_primary_selection_source_v1* s) { (void)d; (void)s; }
static const struct zwp_primary_selection_source_v1_listener src_listener = {src_send, src_cancelled};

static void offer_offer(void* d, struct zwp_primary_selection_offer_v1* o, const char* m) {
    (void)d; (void)o; (void)m;
}
static const struct zwp_primary_selection_offer_v1_listener offer_listener = {offer_offer};

static void dev_data_offer(void* d, struct zwp_primary_selection_device_v1* dd,
                           struct zwp_primary_selection_offer_v1* o) {
    (void)d; (void)dd;
    zwp_primary_selection_offer_v1_add_listener(o, &offer_listener, NULL);
}
static void dev_selection(void* d, struct zwp_primary_selection_device_v1* dd,
                          struct zwp_primary_selection_offer_v1* o) {
    (void)d; (void)dd;
    if (o) { current_offer = o; selection_offered = 1; }
}
static const struct zwp_primary_selection_device_v1_listener dev_listener = {dev_data_offer, dev_selection};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_primary_selection_device_manager_v1_interface.name))
        pmgr = wl_registry_bind(r, name, &zwp_primary_selection_device_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_seat_g || !wl_kbd) { fprintf(stderr, "missing globals\n"); return 1; }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!pmgr) { fprintf(stderr, "no primary-selection manager\n"); return 1; }

    wl_make_toplevel(&top, "client_feat_primary_selection", 400, 300, 0xFFFF0000);

    for (int i = 0; i < 200 && !wlk_key_serial; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!wlk_key_serial) { fprintf(stderr, "no key serial\n"); return 1; }

    struct zwp_primary_selection_device_v1* dev =
        zwp_primary_selection_device_manager_v1_get_device(pmgr, wl_seat_g);
    zwp_primary_selection_device_v1_add_listener(dev, &dev_listener, NULL);

    struct zwp_primary_selection_source_v1* src =
        zwp_primary_selection_device_manager_v1_create_source(pmgr);
    zwp_primary_selection_source_v1_add_listener(src, &src_listener, NULL);
    zwp_primary_selection_source_v1_offer(src, "text/plain");
    zwp_primary_selection_device_v1_set_selection(dev, src, wlk_key_serial);

    for (int i = 0; i < 100 && !selection_offered; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!selection_offered) { fprintf(stderr, "no primary selection offer\n"); return 1; }

    int fds[2];
    if (pipe(fds) < 0) { perror("pipe"); return 1; }
    zwp_primary_selection_offer_v1_receive(current_offer, "text/plain", fds[1]);
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

    printf("client_feat_primary_selection: read '%s'\n", buf);
    if (strcmp(buf, PAYLOAD) != 0) { fprintf(stderr, "payload mismatch\n"); return 1; }
    printf("client_feat_primary_selection: ok\n");
    return 0;
}
