#include "wl_util.h"

#include <color-management-v1-client-protocol.h>

// An output whose link depth or RGB range changes while its colour stays
// the same: the image description a client holds describes primaries,
// transfer and luminances, none of which moved, so it hears no
// image_description_changed. Counts the events over presented frames,
// then again after the scenario has changed the range.

static struct wp_color_manager_v1* cm;
static struct wl_output* output;
static int changed;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        cm = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(r, name, &wl_output_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void cm_output_changed(void* d, struct wp_color_management_output_v1* o) {
    (void)d; (void)o;
    changed++;
}
static const struct wp_color_management_output_v1_listener cm_output_listener = {
    .image_description_changed = cm_output_changed,
};

static void frames(struct wl_toplevel_ctx* ctx, int n) {
    for (int i = 0; i < n; i++) {
        wl_await_presented(ctx->surface);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!cm || !output) return 2;

    wp_color_management_output_v1_add_listener(wp_color_manager_v1_get_output(cm, output), &cm_output_listener, NULL);

    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "depth-quiet", 160, 120, 0xff406080);
    frames(&ctx, 10);
    printf("changes after boot %d\n", changed);

    if (wl_await_file("go-range")) {
        return 1;
    }

    frames(&ctx, 10);
    printf("changes after range %d\n", changed);

    if (changed) {
        fprintf(stderr, "the description changed %d times for a colour that did not\n", changed);
        return 1;
    }

    return 0;
}
