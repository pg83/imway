#include "wl_util.h"

#ifndef OUTPUT_VERSION
#error OUTPUT_VERSION is required
#endif

static struct wl_output* test_output;
static int geometry, mode, done, scale, name_ev, description;

static void out_geometry(void* d, struct wl_output* o, int32_t x, int32_t y,
                         int32_t pw, int32_t ph, int32_t subpixel,
                         const char* make, const char* model, int32_t transform) {
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)subpixel;
    (void)make; (void)model; (void)transform; geometry++;
}
static void out_mode(void* d, struct wl_output* o, uint32_t flags,
                     int32_t w, int32_t h, int32_t refresh) {
    (void)d; (void)o; (void)flags; (void)w; (void)h; (void)refresh; mode++;
}
static void out_done(void* d, struct wl_output* o) { (void)d; (void)o; done++; }
static void out_scale(void* d, struct wl_output* o, int32_t factor) {
    (void)d; (void)o; (void)factor; scale++;
}
static void out_name(void* d, struct wl_output* o, const char* value) {
    (void)d; (void)o; (void)value; name_ev++;
}
static void out_description(void* d, struct wl_output* o, const char* value) {
    (void)d; (void)o; (void)value; description++;
}
static const struct wl_output_listener out_listener = {
    out_geometry, out_mode, out_done, out_scale, out_name, out_description,
};

static void output_global(void* d, struct wl_registry* registry, uint32_t id,
                          const char* interface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(interface, wl_output_interface.name) && !test_output) {
        test_output = wl_registry_bind(registry, id, &wl_output_interface,
                                       OUTPUT_VERSION);
        wl_output_add_listener(test_output, &out_listener, NULL);
    }
}
static void output_remove(void* d, struct wl_registry* registry, uint32_t id) {
    (void)d; (void)registry; (void)id;
}
static const struct wl_registry_listener output_registry_listener = {
    output_global, output_remove,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    if (wl_boot()) return 1;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &output_registry_listener, NULL);
    if (wl_display_roundtrip(wl_dpy) < 0 ||
        wl_display_roundtrip(wl_dpy) < 0 || !test_output) return 1;

    int ok = geometry && mode;
    if (OUTPUT_VERSION == 1)
        ok = ok && !done && !scale && !name_ev && !description;
    else if (OUTPUT_VERSION == 2)
        ok = ok && done && scale && !name_ev && !description;
    else
        ok = ok && done && scale && name_ev && description;

    printf("output-v%d geometry=%d mode=%d done=%d scale=%d name=%d description=%d\n",
           OUTPUT_VERSION, geometry, mode, done, scale, name_ev, description);
    return ok ? 0 : 1;
}
