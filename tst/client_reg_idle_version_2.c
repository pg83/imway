// ext-idle-notify v2: the compositor must advertise version >= 2 and support
// get_input_idle_notification, whose notification ignores idle inhibitors.
// This client holds an idle inhibitor, then asks for an input idle
// notification with a short timeout; it must still fire.

#include "wl_util.h"
#include <ext-idle-notify-v1-client-protocol.h>
#include <idle-inhibit-unstable-v1-client-protocol.h>

static struct ext_idle_notifier_v1* notifier;
static struct zwp_idle_inhibit_manager_v1* inhibit_mgr;
static uint32_t notifier_version;
static int idled;

static void notif_idled(void* d, struct ext_idle_notification_v1* n) { (void)d;(void)n; idled = 1; }
static void notif_resumed(void* d, struct ext_idle_notification_v1* n) { (void)d;(void)n; }
static const struct ext_idle_notification_v1_listener notif_listener = {
    .idled = notif_idled, .resumed = notif_resumed,
};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, ext_idle_notifier_v1_interface.name)) {
        notifier_version = v;
        notifier = wl_registry_bind(r, name, &ext_idle_notifier_v1_interface, v < 2 ? v : 2);
    } else if (!strcmp(iface, zwp_idle_inhibit_manager_v1_interface.name)) {
        inhibit_mgr = wl_registry_bind(r, name, &zwp_idle_inhibit_manager_v1_interface, 1);
    }
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!notifier || !inhibit_mgr) {
        fprintf(stderr, "missing idle globals\n");
        return 1;
    }
    if (notifier_version < 2) {
        fprintf(stderr, "ext_idle_notifier at version %u, want >= 2\n", notifier_version);
        return 1;
    }

    // map a surface and hold an idle inhibitor on it: a plain idle
    // notification would be blocked, an input idle notification must not be
    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "idle2", 200, 150, 0xFF806040u);
    struct zwp_idle_inhibitor_v1* inhibitor =
        zwp_idle_inhibit_manager_v1_create_inhibitor(inhibit_mgr, top.surface);
    (void)inhibitor;

    struct ext_idle_notification_v1* n =
        ext_idle_notifier_v1_get_input_idle_notification(notifier, 200, wl_seat_g);
    ext_idle_notification_v1_add_listener(n, &notif_listener, NULL);
    wl_display_flush(wl_dpy);

    for (int i = 0; i < 200 && !idled; i++) {
        if (wl_display_dispatch(wl_dpy) < 0) break;
    }

    if (!idled) {
        fprintf(stderr, "input idle notification did not fire despite short timeout\n");
        return 1;
    }

    printf("client_reg_idle_version_2: input idle fired past inhibitor\n");
    return 0;
}
