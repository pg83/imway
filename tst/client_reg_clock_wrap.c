// Across the wrap of the compositor's 32-bit millisecond clock: a black
// window keeps ringing the bell every 30ms until "go-stop" appears in
// XDG_RUNTIME_DIR, prints the timestamp of every relative motion it gets
// ("rel <utime>"), and exits on "go-exit".

#include "wl_util.h"
#include <relative-pointer-unstable-v1-client-protocol.h>
#include <xdg-system-bell-v1-client-protocol.h>

static struct xdg_system_bell_v1* bell;
static struct zwp_relative_pointer_manager_v1* rel_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_system_bell_v1_interface.name))
        bell = wl_registry_bind(r, name, &xdg_system_bell_v1_interface, 1);
    else if (!strcmp(iface, zwp_relative_pointer_manager_v1_interface.name))
        rel_mgr = wl_registry_bind(r, name, &zwp_relative_pointer_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void rel_motion(void* d, struct zwp_relative_pointer_v1* rp, uint32_t hi, uint32_t lo,
                       wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t udx, wl_fixed_t udy) {
    (void)d; (void)rp; (void)dx; (void)dy; (void)udx; (void)udy;
    printf("rel %llu\n", (unsigned long long)(((uint64_t)hi << 32) | lo));
}
static const struct zwp_relative_pointer_v1_listener rel_listener = {rel_motion};

static int have(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);
    return access(path, F_OK) == 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(55);
    if (wl_boot() || !wl_ptr) return 2;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!bell || !rel_mgr) return 2;

    struct zwp_relative_pointer_v1* rp = zwp_relative_pointer_manager_v1_get_relative_pointer(rel_mgr, wl_ptr);
    zwp_relative_pointer_v1_add_listener(rp, &rel_listener, NULL);

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "clock-wrap", 300, 200, 0xFF000000u);
    printf("ringing\n");

    while (!have("stop")) {
        xdg_system_bell_v1_ring(bell, top.surface);
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(30000);
    }
    printf("stopped\n");

    while (!have("exit")) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }
    return 0;
}
