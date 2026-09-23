// A presentation feedback that libwayland tears down before its surface at
// disconnect. Client objects die in id order, and ids are reused lowest
// first: freeing an early region and then asking for feedback gives the
// feedback an id below its surface's, so the pending feedback goes while the
// surface still holds it in its list.

#include "wl_util.h"
#include <presentation-time-client-protocol.h>

static struct wp_presentation* presentation;
// the objects the disconnect leaves behind: libwayland frees neither the
// surface proxy nor the pending feedbacks, and statics keep them reachable
// for the leak checker
static struct wl_registry* registry;
static struct wl_surface* surface;
static struct wp_presentation_feedback* feedbacks[8];

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_presentation_interface.name))
        presentation = wl_registry_bind(r, name, &wp_presentation_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);

    if (wl_boot()) return 2;

    registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!presentation) return 2;

    struct wl_region* early = wl_compositor_create_region(wl_comp);
    surface = wl_compositor_create_surface(wl_comp);

    // the region's id comes free once the compositor confirms the delete
    wl_region_destroy(early);
    wl_display_roundtrip(wl_dpy);

    // the roundtrip's own callback freed an id too, and freed ids come back
    // last-freed first: keep asking until one of them lands below the
    // surface (the ones above it are pending feedbacks just the same)
    struct wp_presentation_feedback* fb = NULL;

    for (int i = 0; i < 8; i++) {
        fb = wp_presentation_feedback(presentation, surface);
        feedbacks[i] = fb;

        if (wl_proxy_get_id((struct wl_proxy*)fb) < wl_proxy_get_id((struct wl_proxy*)surface))
            break;
    }

    if (wl_proxy_get_id((struct wl_proxy*)fb) > wl_proxy_get_id((struct wl_proxy*)surface)) {
        fprintf(stderr, "the feedback did not reuse the early id\n");
        return 2;
    }

    wl_surface_commit(surface);
    wl_display_roundtrip(wl_dpy);
    printf("feedback pending below its surface\n");

    // disconnecting tears the feedback down first
    wl_display_disconnect(wl_dpy);

    return 0;
}
