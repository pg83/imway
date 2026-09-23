// xdg-system-bell flash: a black window keeps ringing the bell every 30ms,
// so a flash is lit whenever the scenario looks, until "stop-go" appears in
// XDG_RUNTIME_DIR; then it rings no more and waits for "done-go" to exit.

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

static int have(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", getenv("XDG_RUNTIME_DIR"), name);
    return access(path, F_OK) == 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(90);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!bell) {
        fprintf(stderr, "no xdg_system_bell\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "bellflash", 300, 200, 0xFF000000u);
    puts("client_reg_system_bell_flash: ringing");

    while (!have("stop-go")) {
        xdg_system_bell_v1_ring(bell, top.surface);
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "connection lost\n");
            return 2;
        }
        usleep(30000);
    }
    puts("client_reg_system_bell_flash: stopped");

    while (!have("done-go")) {
        if (wl_display_roundtrip(wl_dpy) < 0) {
            fprintf(stderr, "connection lost\n");
            return 3;
        }
        usleep(20000);
    }

    xdg_system_bell_v1_destroy(bell);
    wl_registry_destroy(reg2);
    wl_display_roundtrip(wl_dpy);
    puts("client_reg_system_bell_flash: done");
    return 0;
}
