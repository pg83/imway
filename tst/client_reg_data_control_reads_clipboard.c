#include "wl_util.h"

#include <ext-data-control-v1-client-protocol.h>
#include <primary-selection-unstable-v1-client-protocol.h>

// A clipboard manager (ext-data-control) reads what an ordinary client put
// on the clipboard (wl_data_device) and in the primary selection
// (zwp_primary_selection): its offer's receive must reach that source.

static struct ext_data_control_manager_v1* mgr;
static struct zwp_primary_selection_device_manager_v1* primary_mgr;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, ext_data_control_manager_v1_interface.name))
        mgr = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_primary_selection_device_manager_v1_interface.name))
        primary_mgr = wl_registry_bind(registry, name,
                                       &zwp_primary_selection_device_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void write_payload(int32_t fd, const char* payload) {
    if (write(fd, payload, strlen(payload)) < 0)
        perror("write");
    close(fd);
}

// ---- the ordinary client's sources ----
static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    write_payload(fd, "from-clipboard");
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_drop(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action,
};

static void psrc_send(void* d, struct zwp_primary_selection_source_v1* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    write_payload(fd, "from-primary");
}
static void psrc_cancelled(void* d, struct zwp_primary_selection_source_v1* s) { (void)d; (void)s; }
static const struct zwp_primary_selection_source_v1_listener psrc_listener = {psrc_send, psrc_cancelled};

// ---- the clipboard manager's device ----
static struct ext_data_control_offer_v1* clip_offer;
static struct ext_data_control_offer_v1* primary_offer;

static void dc_data_offer(void* d, struct ext_data_control_device_v1* dev, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dev; (void)o;
}
static void dc_selection(void* d, struct ext_data_control_device_v1* dev, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dev;
    clip_offer = o;
}
static void dc_finished(void* d, struct ext_data_control_device_v1* dev) { (void)d; (void)dev; }
static void dc_primary(void* d, struct ext_data_control_device_v1* dev, struct ext_data_control_offer_v1* o) {
    (void)d; (void)dev;
    primary_offer = o;
}
static const struct ext_data_control_device_v1_listener dc_listener = {
    dc_data_offer, dc_selection, dc_finished, dc_primary,
};

// receives through the offer and returns what arrived before the pipe closed
static int receive(struct ext_data_control_offer_v1* offer, char* buf, size_t size) {
    int fds[2];

    if (pipe(fds) < 0) return -1;
    ext_data_control_offer_v1_receive(offer, "text/plain", fds[1]);
    close(fds[1]);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    size_t n = 0;
    ssize_t r;

    memset(buf, 0, size);
    while (n < size - 1 && (r = read(fds[0], buf + n, size - 1 - n)) > 0) {
        n += (size_t)r;
    }
    close(fds[0]);
    return (int)n;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!mgr || !primary_mgr || !wl_ddm) {
        fprintf(stderr, "missing globals\n");
        return 2;
    }

    struct ext_data_control_device_v1* dc = ext_data_control_manager_v1_get_data_device(mgr, wl_seat_g);
    ext_data_control_device_v1_add_listener(dc, &dc_listener, NULL);

    // the ordinary client: focused, so its serial may set the selections
    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "dc-reads-clipboard", 160, 120, 0xff008000);
    for (int i = 0; i < 200 && !wlk_enters; i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(10000);
    }
    if (!wlk_enters) {
        fprintf(stderr, "the window never took the keyboard\n");
        return 1;
    }

    struct wl_data_device* dev = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    wl_data_source_offer(src, "text/plain");
    wl_data_device_set_selection(dev, src, wlk_enter_serial);

    struct zwp_primary_selection_device_v1* pdev =
        zwp_primary_selection_device_manager_v1_get_device(primary_mgr, wl_seat_g);
    struct zwp_primary_selection_source_v1* psrc =
        zwp_primary_selection_device_manager_v1_create_source(primary_mgr);
    zwp_primary_selection_source_v1_add_listener(psrc, &psrc_listener, NULL);
    zwp_primary_selection_source_v1_offer(psrc, "text/plain");
    zwp_primary_selection_device_v1_set_selection(pdev, psrc, wlk_enter_serial);

    for (int i = 0; i < 200 && (!clip_offer || !primary_offer); i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(10000);
    }
    if (!clip_offer || !primary_offer) {
        fprintf(stderr, "no data-control offer (clipboard %p, primary %p)\n", (void*)clip_offer, (void*)primary_offer);
        return 1;
    }

    char buf[64];

    if (receive(clip_offer, buf, sizeof(buf)) < 0 || strcmp(buf, "from-clipboard")) {
        fprintf(stderr, "the clipboard read through data control got \"%s\"\n", buf);
        return 1;
    }
    if (receive(primary_offer, buf, sizeof(buf)) < 0 || strcmp(buf, "from-primary")) {
        fprintf(stderr, "the primary selection read through data control got \"%s\"\n", buf);
        return 1;
    }

    printf("data-control read both selections\n");
    return 0;
}
