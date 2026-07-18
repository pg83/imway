/* PLAN limits #3: more than 64 MIME offers on one selection source. The cap
 * keeps the first 64 entries intact and drops the rest; the focused client
 * (us) reads its own offer back and counts. */
#include "wl_util.h"

static int mime_count;
static int mime_prefix_ok = 1;
static int selection_seen;

static void offer_mime(void* d, struct wl_data_offer* offer, const char* mime) {
    (void)d; (void)offer;
    char want[64];
    snprintf(want, sizeof(want), "application/x-flood-%d", mime_count);
    if (mime_count < 64 && strcmp(mime, want)) mime_prefix_ok = 0;
    mime_count++;
}
static void offer_actions(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static void offer_action(void* d, struct wl_data_offer* o, uint32_t a) { (void)d; (void)o; (void)a; }
static const struct wl_data_offer_listener offer_listener = {offer_mime, offer_actions,
                                                             offer_action};

static void dd_data_offer(void* d, struct wl_data_device* dd, struct wl_data_offer* offer) {
    (void)d; (void)dd;
    wl_data_offer_add_listener(offer, &offer_listener, NULL);
}
static void dd_enter(void* d, struct wl_data_device* dd, uint32_t serial, struct wl_surface* s,
                     wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* offer) {
    (void)d; (void)dd; (void)serial; (void)s; (void)x; (void)y; (void)offer;
}
static void dd_leave(void* d, struct wl_data_device* dd) { (void)d; (void)dd; }
static void dd_motion(void* d, struct wl_data_device* dd, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dd; (void)t; (void)x; (void)y;
}
static void dd_drop(void* d, struct wl_data_device* dd) { (void)d; (void)dd; }
static void dd_selection(void* d, struct wl_data_device* dd, struct wl_data_offer* offer) {
    (void)d; (void)dd;
    if (offer) selection_seen = 1;
}
static const struct wl_data_device_listener dd_listener = {dd_data_offer, dd_enter, dd_leave,
                                                           dd_motion, dd_drop, dd_selection};

static void src_target(void* d, struct wl_data_source* s, const char* m) { (void)d; (void)s; (void)m; }
static void src_send(void* d, struct wl_data_source* s, const char* m, int32_t fd) {
    (void)d; (void)s; (void)m;
    close(fd);
}
static void src_cancelled(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_performed(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_dnd_finished(void* d, struct wl_data_source* s) { (void)d; (void)s; }
static void src_action(void* d, struct wl_data_source* s, uint32_t a) { (void)d; (void)s; (void)a; }
static const struct wl_data_source_listener src_listener = {
    src_target, src_send, src_cancelled, src_dnd_performed, src_dnd_finished, src_action};

int main(void) {
    alarm(10);
    if (wl_boot() || !wl_ddm || !wl_seat_g) return 1;

    struct wl_data_device* dd = wl_data_device_manager_get_data_device(wl_ddm, wl_seat_g);
    wl_data_device_add_listener(dd, &dd_listener, NULL);

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "mime-flood", 64, 64, 0xffff0000);
    while (!wlk_enter_serial && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wl_data_source* src = wl_data_device_manager_create_data_source(wl_ddm);
    wl_data_source_add_listener(src, &src_listener, NULL);
    char mime[64];
    for (int i = 0; i < 128; i++) {
        snprintf(mime, sizeof(mime), "application/x-flood-%d", i);
        wl_data_source_offer(src, mime);
    }
    wl_data_device_set_selection(dd, src, wlk_enter_serial);
    while (!selection_seen && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (!selection_seen || mime_count != 64 || !mime_prefix_ok) {
        fprintf(stderr, "selection=%d mimes=%d prefix_ok=%d, want 64 intact\n", selection_seen,
                mime_count, mime_prefix_ok);
        return 1;
    }
    return 0;
}
