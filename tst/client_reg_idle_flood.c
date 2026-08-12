/* PLAN limits #5: hundreds of idle notifications and inhibitors, then an
 * abrupt disconnect with all of them still live. */
#include "wl_util.h"

#include <ext-idle-notify-v1-client-protocol.h>
#include <idle-inhibit-unstable-v1-client-protocol.h>

static struct ext_idle_notifier_v1* notifier;
static struct zwp_idle_inhibit_manager_v1* inhibit_mgr;

static void idle_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, ext_idle_notifier_v1_interface.name))
        notifier = wl_registry_bind(r, name, &ext_idle_notifier_v1_interface, 1);
    else if (!strcmp(iface, zwp_idle_inhibit_manager_v1_interface.name))
        inhibit_mgr = wl_registry_bind(r, name, &zwp_idle_inhibit_manager_v1_interface, 1);
}
static void idle_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener idle_listener = {idle_global, idle_remove};

int main(void) {
    alarm(30);
    if (wl_boot() || !wl_seat_g) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &idle_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!notifier || !inhibit_mgr) return 77;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "idle-flood", 64, 64, 0xffff0000);

    for (int i = 0; i < 256; i++)
        ext_idle_notifier_v1_get_idle_notification(notifier, 100000 + i, wl_seat_g);
    for (int i = 0; i < 256; i++)
        zwp_idle_inhibit_manager_v1_create_inhibitor(inhibit_mgr, top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

    return 0; /* vanish with everything still registered */
}
