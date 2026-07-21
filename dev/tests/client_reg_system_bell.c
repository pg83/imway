// xdg-system-bell: the compositor must advertise the global and accept ring.

#include "wl_util.h"
#include <xdg-system-bell-v1-client-protocol.h>

static struct xdg_system_bell_v1* bell;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_system_bell_v1_interface.name))
        bell = wl_registry_bind(r, name, &xdg_system_bell_v1_interface, 1);
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
    if (!bell) {
        fprintf(stderr, "no xdg_system_bell\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "bell", 200, 150, 0xFF5050A0u);

    // ring without and with a surface argument
    xdg_system_bell_v1_ring(bell, NULL);
    xdg_system_bell_v1_ring(bell, top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_system_bell: rang\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
