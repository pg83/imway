/* One input method per seat. A second zwp_input_method_v2 on the same seat
 * must come back inert — bound, then immediately unavailable — instead of
 * taking the seat from the first one or killing the client. */

#include "wl_util.h"

#include <input-method-unstable-v2-client-protocol.h>

static struct zwp_input_method_manager_v2* im_mgr;
static int first_unavailable, second_unavailable;
static int first_activate, second_activate;

static void im_activate(void* d, struct zwp_input_method_v2* im) {
    (void)im;
    if (d) second_activate++;
    else first_activate++;
}
static void im_deactivate(void* d, struct zwp_input_method_v2* im) { (void)d; (void)im; }
static void im_surrounding_text(void* d, struct zwp_input_method_v2* im, const char* text,
                                uint32_t cursor, uint32_t anchor) {
    (void)d; (void)im; (void)text; (void)cursor; (void)anchor;
}
static void im_text_change_cause(void* d, struct zwp_input_method_v2* im, uint32_t cause) {
    (void)d; (void)im; (void)cause;
}
static void im_content_type(void* d, struct zwp_input_method_v2* im, uint32_t hint,
                            uint32_t purpose) {
    (void)d; (void)im; (void)hint; (void)purpose;
}
static void im_done(void* d, struct zwp_input_method_v2* im) { (void)d; (void)im; }
static void im_unavailable(void* d, struct zwp_input_method_v2* im) {
    (void)im;
    if (d) second_unavailable++;
    else first_unavailable++;
}

static const struct zwp_input_method_v2_listener im_listener = {
    im_activate, im_deactivate, im_surrounding_text, im_text_change_cause,
    im_content_type, im_done, im_unavailable,
};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t ver) {
    (void)d; (void)ver;
    if (!strcmp(iface, zwp_input_method_manager_v2_interface.name))
        im_mgr = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot() || !wl_seat_g) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!im_mgr) {
        fprintf(stderr, "no input-method manager\n");
        return 2;
    }

    struct zwp_input_method_v2* first =
        zwp_input_method_manager_v2_get_input_method(im_mgr, wl_seat_g);

    zwp_input_method_v2_add_listener(first, &im_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    struct zwp_input_method_v2* second =
        zwp_input_method_manager_v2_get_input_method(im_mgr, wl_seat_g);

    zwp_input_method_v2_add_listener(second, &im_listener, (void*)1);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    printf("first unavailable=%d second unavailable=%d\n", first_unavailable, second_unavailable);

    if (first_unavailable || !second_unavailable) {
        fprintf(stderr, "the wrong input method was made inert\n");
        return 1;
    }

    /* the inert one may be destroyed without disturbing the live one */
    zwp_input_method_v2_destroy(second);
    wl_display_roundtrip(wl_dpy);

    if (wl_display_get_error(wl_dpy)) {
        fprintf(stderr, "the connection broke: %d\n", wl_display_get_error(wl_dpy));
        return 1;
    }

    printf("second input method inert (activations %d/%d)\n", first_activate, second_activate);

    return 0;
}
