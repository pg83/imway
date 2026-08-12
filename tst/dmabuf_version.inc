#include "wl_util.h"
#include <linux-dmabuf-v1-client-protocol.h>

#ifndef DMABUF_VERSION
#error DMABUF_VERSION is required
#endif

static struct zwp_linux_dmabuf_v1* dmabuf;
static int formats, modifiers;
static int fb_done, fb_table, fb_main, fb_tranche_done, fb_target, fb_formats, fb_flags;

static void legacy_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format) {
    (void)d; (void)z; (void)format; formats++;
}
static void legacy_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t format,
                            uint32_t hi, uint32_t lo) {
    (void)d; (void)z; (void)format; (void)hi; (void)lo; modifiers++;
}
static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    legacy_format, legacy_modifier,
};
static void feedback_done(void* d, struct zwp_linux_dmabuf_feedback_v1* f) {
    (void)d; (void)f; fb_done++;
}
static void feedback_table(void* d, struct zwp_linux_dmabuf_feedback_v1* f,
                           int32_t fd, uint32_t size) {
    (void)d; (void)f; if (size) fb_table++; close(fd);
}
static void feedback_main(void* d, struct zwp_linux_dmabuf_feedback_v1* f,
                          struct wl_array* device) {
    (void)d; (void)f; if (device->size) fb_main++;
}
static void feedback_tranche_done(void* d, struct zwp_linux_dmabuf_feedback_v1* f) {
    (void)d; (void)f; fb_tranche_done++;
}
static void feedback_target(void* d, struct zwp_linux_dmabuf_feedback_v1* f,
                            struct wl_array* device) {
    (void)d; (void)f; if (device->size) fb_target++;
}
static void feedback_formats(void* d, struct zwp_linux_dmabuf_feedback_v1* f,
                             struct wl_array* indices) {
    (void)d; (void)f; if (indices->size) fb_formats++;
}
static void feedback_flags(void* d, struct zwp_linux_dmabuf_feedback_v1* f,
                           uint32_t flags) {
    (void)d; (void)f; (void)flags; fb_flags++;
}
static const struct zwp_linux_dmabuf_feedback_v1_listener feedback_listener = {
    feedback_done, feedback_table, feedback_main, feedback_tranche_done,
    feedback_target, feedback_formats, feedback_flags,
};
static void dmabuf_global(void* d, struct wl_registry* registry, uint32_t id,
                          const char* interface, uint32_t version) {
    (void)d;
    if (!strcmp(interface, zwp_linux_dmabuf_v1_interface.name) &&
        version >= DMABUF_VERSION && !dmabuf) {
        dmabuf = wl_registry_bind(registry, id, &zwp_linux_dmabuf_v1_interface,
                                  DMABUF_VERSION);
        zwp_linux_dmabuf_v1_add_listener(dmabuf, &dmabuf_listener, NULL);
    }
}
static void dmabuf_remove(void* d, struct wl_registry* registry, uint32_t id) {
    (void)d; (void)registry; (void)id;
}
static const struct wl_registry_listener dmabuf_registry_listener = {
    dmabuf_global, dmabuf_remove,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &dmabuf_registry_listener, NULL);
    if (wl_display_roundtrip(wl_dpy) < 0 || !dmabuf) return 77;
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;

#if DMABUF_VERSION >= 4
    struct zwp_linux_dmabuf_feedback_v1* feedback =
        zwp_linux_dmabuf_v1_get_default_feedback(dmabuf);
    zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &feedback_listener, NULL);
    if (wl_display_roundtrip(wl_dpy) < 0) return 1;
#endif

    printf("dmabuf-v%d legacy=%d/%d feedback=%d/%d/%d/%d/%d/%d/%d\n",
           DMABUF_VERSION, formats, modifiers, fb_done, fb_table, fb_main,
           fb_tranche_done, fb_target, fb_formats, fb_flags);
#if DMABUF_VERSION >= 4
    return !formats && !modifiers && fb_done && fb_table && fb_main &&
           fb_tranche_done && fb_target && fb_formats && fb_flags ? 0 : 1;
#else
    return !formats && modifiers && !fb_done && !fb_table && !fb_main &&
           !fb_tranche_done && !fb_target && !fb_formats && !fb_flags ? 0 : 1;
#endif
}
