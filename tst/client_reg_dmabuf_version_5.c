// zwp_linux_dmabuf_v1 v5: the compositor must advertise version >= 5 when it
// exposes feedback (a render device is present).

#include "wl_util.h"
#include <linux-dmabuf-v1-client-protocol.h>

static struct zwp_linux_dmabuf_v1* dmabuf;
static uint32_t dmabuf_version;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name)) {
        dmabuf_version = v;
        dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, v < 5 ? v : 5);
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
    if (!dmabuf) {
        fprintf(stderr, "no linux-dmabuf global\n");
        return 1;
    }
    if (dmabuf_version < 5) {
        fprintf(stderr, "linux-dmabuf at version %u, want >= 5\n", dmabuf_version);
        return 1;
    }

    // the v4+ feedback object must still be obtainable at v5
    struct zwp_linux_dmabuf_feedback_v1* fb = zwp_linux_dmabuf_v1_get_default_feedback(dmabuf);
    (void)fb;
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "default feedback failed at v5\n");
        return 1;
    }

    printf("client_reg_dmabuf_version_5: ok\n");
    return 0;
}
