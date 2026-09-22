#include "wl_util.h"
#include <linux-dmabuf-v1-client-protocol.h>

// linux-dmabuf feedback on a KMS output: ahead of the general tranche comes
// a scanout tranche naming the formats the primary plane takes, so a
// fullscreen client can allocate for the direct-scanout bypass. Both the
// default and a surface's feedback carry it.

static struct zwp_linux_dmabuf_v1* dmabuf;

static void dmabuf_global(void* d, struct wl_registry* registry, uint32_t id,
                          const char* interface, uint32_t version) {
    (void)d;
    if (!strcmp(interface, zwp_linux_dmabuf_v1_interface.name) && version >= 4 && !dmabuf)
        dmabuf = wl_registry_bind(registry, id, &zwp_linux_dmabuf_v1_interface, 4);
}
static void dmabuf_remove(void* d, struct wl_registry* registry, uint32_t id) {
    (void)d; (void)registry; (void)id;
}
static const struct wl_registry_listener dmabuf_registry_listener = {dmabuf_global, dmabuf_remove};

struct feedback {
    int done, tranches, scanout_tranches, scanout_formats;
    int pending_formats;
};

static void fb_done(void* d, struct zwp_linux_dmabuf_feedback_v1* f) {
    (void)f;
    ((struct feedback*)d)->done = 1;
}
static void fb_table(void* d, struct zwp_linux_dmabuf_feedback_v1* f, int32_t fd, uint32_t size) {
    (void)d; (void)f; (void)size;
    close(fd);
}
static void fb_main(void* d, struct zwp_linux_dmabuf_feedback_v1* f, struct wl_array* dev) {
    (void)d; (void)f; (void)dev;
}
static void fb_tranche_done(void* d, struct zwp_linux_dmabuf_feedback_v1* f) {
    (void)f;
    ((struct feedback*)d)->tranches++;
}
static void fb_target(void* d, struct zwp_linux_dmabuf_feedback_v1* f, struct wl_array* dev) {
    (void)d; (void)f; (void)dev;
}
static void fb_formats(void* d, struct zwp_linux_dmabuf_feedback_v1* f, struct wl_array* indices) {
    (void)f;
    ((struct feedback*)d)->pending_formats = (int)(indices->size / sizeof(uint16_t));
}
static void fb_flags(void* d, struct zwp_linux_dmabuf_feedback_v1* f, uint32_t flags) {
    (void)f;
    struct feedback* fb = d;

    if (flags & ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT) {
        fb->scanout_tranches++;
        fb->scanout_formats += fb->pending_formats;
    }
}
static const struct zwp_linux_dmabuf_feedback_v1_listener feedback_listener = {
    fb_done, fb_table, fb_main, fb_tranche_done, fb_target, fb_formats, fb_flags,
};

static int check(const char* what, struct zwp_linux_dmabuf_feedback_v1* f, struct feedback* fb) {
    zwp_linux_dmabuf_feedback_v1_add_listener(f, &feedback_listener, fb);

    while (!fb->done && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("%s: tranches=%d scanout=%d scanout_formats=%d\n", what, fb->tranches,
           fb->scanout_tranches, fb->scanout_formats);

    return fb->done && fb->tranches == 2 && fb->scanout_tranches == 1 && fb->scanout_formats > 0 ? 0 : 1;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &dmabuf_registry_listener, NULL);

    if (wl_display_roundtrip(wl_dpy) < 0 || !dmabuf) return 77;

    struct feedback def = {0}, surf = {0};

    if (check("default", zwp_linux_dmabuf_v1_get_default_feedback(dmabuf), &def) ||
        check("surface", zwp_linux_dmabuf_v1_get_surface_feedback(dmabuf, wl_compositor_create_surface(wl_comp)), &surf)) {
        return 1;
    }

    printf("scanout feedback done\n");

    return 0;
}
