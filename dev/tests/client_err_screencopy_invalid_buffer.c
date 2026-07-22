#include "wl_util.h"

#include <wlr-screencopy-unstable-v1-client-protocol.h>

static struct zwlr_screencopy_manager_v1* mgr;
static struct wl_output* output;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name))
        mgr = wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(registry, name, &wl_output_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!mgr || !output) return 2;

    struct zwlr_screencopy_frame_v1* frame =
        zwlr_screencopy_manager_v1_capture_output(mgr, 0, output);

    // a 4x4 buffer can never match the announced output size
    struct wl_buffer* tiny = wl_solid(4, 4, 0xff000000);
    zwlr_screencopy_frame_v1_copy(frame, tiny);
    return wl_expect_error(zwlr_screencopy_frame_v1_interface.name,
                           ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER);
}
