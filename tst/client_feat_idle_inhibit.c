// Feature: zwp_idle_inhibit on a mapped surface must hold off
// ext-idle-notify; destroying the inhibitor lets the idle timer fire.

#include "wl_util.h"
#include <ext-idle-notify-v1-client-protocol.h>
#include <idle-inhibit-unstable-v1-client-protocol.h>

static struct ext_idle_notifier_v1* notifier;
static struct zwp_idle_inhibit_manager_v1* inhibit_mgr;
static int idled;

static void on_idled(void* d, struct ext_idle_notification_v1* n) { (void)d; (void)n; idled = 1; }
static void on_resumed(void* d, struct ext_idle_notification_v1* n) { (void)d; (void)n; }
static const struct ext_idle_notification_v1_listener notif_listener = {on_idled, on_resumed};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, ext_idle_notifier_v1_interface.name))
        notifier = wl_registry_bind(r, name, &ext_idle_notifier_v1_interface, 1);
    else if (!strcmp(iface, zwp_idle_inhibit_manager_v1_interface.name))
        inhibit_mgr = wl_registry_bind(r, name, &zwp_idle_inhibit_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!notifier || !inhibit_mgr) { fprintf(stderr, "missing globals\n"); return 1; }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "inhibit", 200, 150, 0xFFFF0000);

    struct zwp_idle_inhibitor_v1* inhibitor =
        zwp_idle_inhibit_manager_v1_create_inhibitor(inhibit_mgr, top.surface);
    wl_display_roundtrip(wl_dpy);

    struct ext_idle_notification_v1* n =
        ext_idle_notifier_v1_get_idle_notification(notifier, 300, wl_seat_g);
    ext_idle_notification_v1_add_listener(n, &notif_listener, NULL);

    // 1.2s of quiet with the inhibitor alive: idled must NOT fire
    for (int i = 0; i < 60; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (idled) { fprintf(stderr, "idled despite an active inhibitor\n"); return 1; }
    printf("held\n");

    zwp_idle_inhibitor_v1_destroy(inhibitor);
    wl_display_flush(wl_dpy);

    for (int i = 0; i < 150 && !idled; i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(20000);
    }
    if (!idled) { fprintf(stderr, "never idled after the inhibitor died\n"); return 1; }
    printf("inhibit ok\n");
    return 0;
}
