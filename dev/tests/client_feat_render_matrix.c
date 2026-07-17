// Parameterized rendering client: one binary drives the whole
// scale x transform x viewport x damage matrix. The buffer is a 120x84
// four-quadrant pattern (TL red, TR green, BL blue, BR yellow); the
// scenario's oracle checks sizes, quadrant layout and the damage phase.
//
//   argv: <scale> <transform 0..7> <vp none|dst|crop> <damage buffer|surface|all>
//
// Phase 1 maps the pattern. On KEY_1 the buffer's TL quadrant turns magenta
// and is committed with damage expressed per the damage mode ("surface"
// only makes sense with transform 0 — the surface-coordinate rect of a
// transformed quadrant would need the very math under test).

#include "wl_util.h"
#include <linux/input-event-codes.h>
#include <viewporter-client-protocol.h>

#define BW 120
#define BH 84

static struct wp_viewporter* viewporter;
static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static struct wl_buffer* buf;
static uint32_t* px;
static int committed;

static int scale = 1, transform;
static const char* vp = "none";
static const char* dmode = "all";

static void paint_quadrants(void) {
    for (int y = 0; y < BH; y++)
        for (int x = 0; x < BW; x++) {
            uint32_t c = y < BH / 2 ? (x < BW / 2 ? 0xFFFF0000 : 0xFF00FF00)
                                    : (x < BW / 2 ? 0xFF0000FF : 0xFFFFFF00);
            px[y * BW + x] = c;
        }
}

static struct wl_buffer* mapped_buffer(void) {
    int stride = BW * 4, size = stride * BH;
    int fd = memfd_create("matrix-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { perror("memfd"); exit(1); }
    px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    paint_quadrants();
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* b = wl_shm_pool_create_buffer(pool, 0, BW, BH, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return b;
}

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (committed) return;

    int swapped = transform == 1 || transform == 3 || transform == 5 || transform == 7;
    int vw = (swapped ? BH : BW) / scale;
    int vh = (swapped ? BW : BH) / scale;

    wl_surface_set_buffer_scale(surface, scale);
    wl_surface_set_buffer_transform(surface, transform);

    if (strcmp(vp, "none")) {
        struct wp_viewport* v = wp_viewporter_get_viewport(viewporter, surface);
        if (!strcmp(vp, "dst")) {
            wp_viewport_set_destination(v, vw * 2, vh * 2);
        } else { // crop: the top-left quadrant of the surface, scaled to 100x60
            wp_viewport_set_source(v, wl_fixed_from_int(0), wl_fixed_from_int(0),
                                   wl_fixed_from_int(vw / 2), wl_fixed_from_int(vh / 2));
            wp_viewport_set_destination(v, 100, 60);
        }
    }

    buf = mapped_buffer();
    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, BW, BH);
    wl_surface_commit(surface);
    committed = 1;
    printf("phase1\n");
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* s) {
    (void)d; (void)t; (void)w; (void)h; (void)s;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface,
                        uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (argc >= 2) scale = atoi(argv[1]);
    if (argc >= 3) transform = atoi(argv[2]);
    if (argc >= 4) vp = argv[3];
    if (argc >= 5) dmode = argv[4];
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!viewporter) { fprintf(stderr, "no viewporter\n"); return 1; }

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_app_id(tl, "matrix");
    wl_surface_commit(surface);

    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    // TL quadrant of the BUFFER goes magenta
    for (int y = 0; y < BH / 2; y++)
        for (int x = 0; x < BW / 2; x++) px[y * BW + x] = 0xFFFF00FF;
    wl_surface_attach(surface, buf, 0, 0);
    if (!strcmp(dmode, "buffer")) {
        wl_surface_damage_buffer(surface, 0, 0, BW / 2, BH / 2);
    } else if (!strcmp(dmode, "surface")) {
        wl_surface_damage(surface, 0, 0, BW / 2 / scale, BH / 2 / scale);
    } else {
        wl_surface_damage_buffer(surface, 0, 0, BW, BH);
    }
    wl_surface_commit(surface);
    wl_display_roundtrip(wl_dpy);
    printf("phase2\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
