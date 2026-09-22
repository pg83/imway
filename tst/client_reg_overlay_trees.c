// Surface trees the compositor draws in unusual ways, all at once:
// - a 300x200 toplevel, its left half transparent and its right half blue,
//   with a 40x40 red subsurface at (130,50) stacked below it: only the 20
//   columns under the transparent half show
// - a popup whose 120x100 green buffer is cropped by window geometry to its
//   central 100x80, with a 20x20 yellow subsurface above it and a 30x30
//   magenta one at (-15,40) below it, half of which shows past its left edge
// - on pointer enter, a cursor surface with a 32x32 cyan buffer that a
//   viewport crops to 16x16 and scales to 24x24
// Prints "trees mapped" once the popup is configured and committed and
// "cursor-set" once the cursor is.

#include "wl_util.h"

#include <viewporter-client-protocol.h>

static struct wp_viewporter* viewporter;
static int popup_configured;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
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

static struct wl_buffer* half_blue(int w, int h) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("half-blue", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[y * w + x] = x < w / 2 ? 0 : 0xff0000ff;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static struct wl_surface* child(struct wl_surface* parent, int x, int y, int w, int h, uint32_t argb, int below) {
    struct wl_surface* s = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, s, parent);
    wl_subsurface_set_position(sub, x, y);
    if (below) wl_subsurface_place_below(sub, parent);
    wl_surface_attach(s, wl_solid(w, h, argb), 0, 0);
    wl_surface_damage(s, 0, 0, w, h);
    wl_surface_commit(s);
    return s;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!viewporter || !wl_subcomp) {
        fprintf(stderr, "no wp_viewporter or wl_subcompositor\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "overlay-trees", 300, 200, 0xff0000ff);
    wl_surface_attach(top.surface, half_blue(300, 200), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 300, 200);
    child(top.surface, 130, 50, 40, 40, 0xffff0000, 1);
    wl_surface_commit(top.surface);

    struct wl_surface* ps = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* pxs = xdg_wm_base_get_xdg_surface(wl_wm, ps);
    xdg_surface_add_listener(pxs, &popup_xdg_listener, NULL);
    struct xdg_positioner* pos = xdg_wm_base_create_positioner(wl_wm);
    xdg_positioner_set_size(pos, 100, 80);
    xdg_positioner_set_anchor_rect(pos, 150, 60, 1, 1);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    struct xdg_popup* popup = xdg_surface_get_popup(pxs, top.xs, pos);
    xdg_popup_add_listener(popup, &popup_listener, NULL);
    xdg_positioner_destroy(pos);
    xdg_surface_set_window_geometry(pxs, 10, 10, 100, 80);
    wl_surface_commit(ps);
    while (!popup_configured && wl_display_dispatch(wl_dpy) != -1) {
    }

    child(ps, 50, 40, 20, 20, 0xffffff00, 0);
    child(ps, -15, 40, 30, 30, 0xffff00ff, 1);
    wl_surface_attach(ps, wl_solid(120, 100, 0xff00ff00), 0, 0);
    wl_surface_damage(ps, 0, 0, 120, 100);
    wl_surface_commit(ps);
    wl_display_roundtrip(wl_dpy);
    printf("trees mapped\n");

    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, cursor);
    int cursor_set = 0;

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (wlp_enter_count && !cursor_set && wl_ptr) {
            wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor, 0, 0);
            wp_viewport_set_source(vp, 0, 0, wl_fixed_from_int(16), wl_fixed_from_int(16));
            wp_viewport_set_destination(vp, 24, 24);
            wl_surface_attach(cursor, wl_solid(32, 32, 0xff00ffff), 0, 0);
            wl_surface_damage(cursor, 0, 0, 32, 32);
            wl_surface_commit(cursor);
            wl_display_flush(wl_dpy);
            cursor_set = 1;
            printf("cursor-set\n");
        }
    }
    return 0;
}
