// A popup drawn with the buffer transforms and viewport a window's own
// surfaces take, next to a reference: a 400x200 grey toplevel with a
// subsurface at (20,20), and a popup at (220,20) of its geometry. Both show
// the same 80x40 buffer of four coloured quadrants (red, blue / green,
// yellow). Each phase gives both the same buffer transform (90, 270,
// flipped 90, flipped 270), the last one instead a viewport destination of
// 160x80; the scenario compares the two after "phase N" and releases the
// next with go-N. The toplevel and the popup also carry subsurfaces that
// never got a buffer, above and below.

#include "wl_util.h"

#include <viewporter-client-protocol.h>

static struct wp_viewporter* viewporter;
static int popup_configured;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;

    if (!strcmp(iface, wp_viewporter_interface.name)) {
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
    }
}

static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}

static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void popup_xdg_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    popup_configured = 1;
}

static const struct xdg_surface_listener popup_xdg_listener = {popup_xdg_configure};

static void popup_configure(void* d, struct xdg_popup* p, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d;
    (void)p;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

static void popup_done(void* d, struct xdg_popup* p) {
    (void)d;
    (void)p;
}

static void popup_repositioned(void* d, struct xdg_popup* p, uint32_t token) {
    (void)d;
    (void)p;
    (void)token;
}

static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static void wait_go(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);

    for (int i = 0; i < 1500; i++) {
        if (access(path, F_OK) == 0) {
            return;
        }

        usleep(20000);
        wl_display_roundtrip(wl_dpy);
    }

    fprintf(stderr, "the scenario never released %s\n", name);
    exit(1);
}

static struct wl_buffer* quadrants(void) {
    int w = 80, h = 40, stride = w * 4, size = stride * h;
    int fd = memfd_create("quadrants", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }

    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int right = x >= w / 2, bottom = y >= h / 2;

            px[y * w + x] = bottom ? (right ? 0xffffff00u : 0xff00ff00u) : (right ? 0xff0000ffu : 0xffff0000u);
        }
    }

    munmap(px, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buf;
}

static struct wl_surface* empty_child(struct wl_surface* parent, int below) {
    struct wl_surface* s = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, s, parent);

    wl_subsurface_set_position(sub, 5, 5);

    if (below) {
        wl_subsurface_place_below(sub, parent);
    }

    wl_surface_commit(s);

    return s;
}

static void show(struct wl_surface* s, struct wl_buffer* b, int transform) {
    wl_surface_set_buffer_transform(s, transform);
    wl_surface_attach(s, b, 0, 0);
    wl_surface_damage_buffer(s, 0, 0, 80, 40);
    wl_surface_commit(s);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(120);

    if (wl_boot()) {
        return 1;
    }

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!viewporter || !wl_subcomp) {
        fprintf(stderr, "no wp_viewporter or wl_subcompositor\n");
        return 1;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "overlay-transforms", 400, 200, 0xff808080u);

    struct wl_buffer* quad = quadrants();
    struct wl_surface* ref = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* refSub = wl_subcompositor_get_subsurface(wl_subcomp, ref, top.surface);

    wl_subsurface_set_position(refSub, 20, 20);
    wl_subsurface_set_desync(refSub);
    empty_child(top.surface, 1);
    empty_child(top.surface, 0);

    struct wl_surface* ps = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wl_wm, ps);

    xdg_surface_add_listener(pxs, &popup_xdg_listener, NULL);

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);

    xdg_positioner_set_size(pos, 80, 80);
    xdg_positioner_set_anchor_rect(pos, 220, 20, 1, 1);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);

    struct xdg_popup* popup = xdg_surface_get_popup(pxs, top.xs, pos);

    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_positioner_destroy(pos);
    wl_surface_commit(ps);

    while (!popup_configured && wl_display_dispatch(wl_dpy) != -1) {
    }

    empty_child(ps, 1);
    empty_child(ps, 0);

    static const int transforms[] = {
        WL_OUTPUT_TRANSFORM_90,
        WL_OUTPUT_TRANSFORM_270,
        WL_OUTPUT_TRANSFORM_FLIPPED_90,
        WL_OUTPUT_TRANSFORM_FLIPPED_270,
    };
    char go[16];

    for (int i = 0; i < 4; i++) {
        show(ref, quad, transforms[i]);
        show(ps, quad, transforms[i]);
        wl_display_roundtrip(wl_dpy);
        printf("phase %d\n", i);
        snprintf(go, sizeof(go), "%d", i);
        wait_go(go);
    }

    struct wp_viewport* refVp = wp_viewporter_get_viewport(viewporter, ref);
    struct wp_viewport* popupVp = wp_viewporter_get_viewport(viewporter, ps);

    wp_viewport_set_destination(refVp, 160, 80);
    wp_viewport_set_destination(popupVp, 160, 80);
    show(ref, quad, WL_OUTPUT_TRANSFORM_NORMAL);
    show(ps, quad, WL_OUTPUT_TRANSFORM_NORMAL);
    wl_display_roundtrip(wl_dpy);
    printf("phase 4\n");
    wait_go("4");

    wp_viewport_destroy(popupVp);
    wp_viewport_destroy(refVp);
    xdg_popup_destroy(popup);
    xdg_surface_destroy(pxs);
    wl_surface_destroy(ps);
    wl_buffer_destroy(quad);
    printf("overlay transforms done\n");

    return 0;
}
