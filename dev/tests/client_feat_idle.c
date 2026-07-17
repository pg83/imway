// Feature: ext-idle-notify. A notification with a short timeout must fire
// idled after that much quiet, and resumed once input arrives again.

#include "wl_util.h"
#include <ext-idle-notify-v1-client-protocol.h>

static struct ext_idle_notifier_v1* notifier;
static int idled, resumed;

static void on_idled(void* d, struct ext_idle_notification_v1* n) { (void)d; (void)n; idled = 1; }
static void on_resumed(void* d, struct ext_idle_notification_v1* n) { (void)d; (void)n; resumed = 1; }
static const struct ext_idle_notification_v1_listener notif_listener = {on_idled, on_resumed};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, ext_idle_notifier_v1_interface.name))
        notifier = wl_registry_bind(r, name, &ext_idle_notifier_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;
    if (!wl_seat_g) { fprintf(stderr, "no seat\n"); return 1; }

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!notifier) { fprintf(stderr, "no ext-idle-notifier\n"); return 1; }

    struct ext_idle_notification_v1* n =
        ext_idle_notifier_v1_get_idle_notification(notifier, 200, wl_seat_g);
    ext_idle_notification_v1_add_listener(n, &notif_listener, NULL);

    // no input → idled fires after the timeout
    for (int i = 0; i < 200 && !idled; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!idled) { fprintf(stderr, "never went idle\n"); return 1; }
    printf("client_feat_idle: idled\n");

    // the scenario now injects input → resumed
    for (int i = 0; i < 200 && !resumed; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!resumed) { fprintf(stderr, "never resumed\n"); return 1; }
    printf("client_feat_idle: resumed\n");
    printf("client_feat_idle: ok\n");
    return 0;
}
