// A wl_output that is already bound has to be told when the mode changes
// under it. The compositor re-sends geometry and mode to every bound output
// when the session remodesets, and a client that learned 1280x800 at boot
// must end up knowing the new size without rebinding anything.

#include "wl_util.h"

static struct wl_output* out;
static int32_t out_w, out_h;
static int modes, geometries, dones;

static void out_geometry(void* d, struct wl_output* o, int32_t x, int32_t y,
                         int32_t pw, int32_t ph, int32_t subpixel,
                         const char* make, const char* model, int32_t transform) {
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph;
    (void)subpixel; (void)make; (void)model; (void)transform;
    geometries++;
}
static void out_mode(void* d, struct wl_output* o, uint32_t flags,
                     int32_t w, int32_t h, int32_t refresh) {
    (void)d; (void)o; (void)refresh;

    if (flags & WL_OUTPUT_MODE_CURRENT) {
        out_w = w;
        out_h = h;
        modes++;
    }
}
static void out_done(void* d, struct wl_output* o) {
    (void)d; (void)o;
    dones++;
}
static void out_scale(void* d, struct wl_output* o, int32_t s) { (void)d; (void)o; (void)s; }
static void out_name(void* d, struct wl_output* o, const char* n) { (void)d; (void)o; (void)n; }
static void out_description(void* d, struct wl_output* o, const char* n) { (void)d; (void)o; (void)n; }
static const struct wl_output_listener out_listener = {
    .geometry = out_geometry,
    .mode = out_mode,
    .done = out_done,
    .scale = out_scale,
    .name = out_name,
    .description = out_description,
};

static void relearn_global(void* d, struct wl_registry* r, uint32_t name,
                           const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, wl_output_interface.name) && !out) {
        out = wl_registry_bind(r, name, &wl_output_interface, v < 2 ? v : 2);
        wl_output_add_listener(out, &out_listener, NULL);
    }
}
static void relearn_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener relearn_listener = {relearn_global, relearn_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) return 2;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg2, &relearn_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!out) {
        fprintf(stderr, "no wl_output to bind\n");
        return 2;
    }

    while (!modes && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("output %dx%d\n", out_w, out_h);

    int32_t was_w = out_w, was_h = out_h;

    // the scenario changes the mode from here; nothing is rebound
    while ((out_w == was_w && out_h == was_h) && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (geometries < 2) {
        fprintf(stderr, "the geometry was not re-sent (%d)\n", geometries);
        return 1;
    }

    printf("output relearned %dx%d\n", out_w, out_h);

    return 0;
}
