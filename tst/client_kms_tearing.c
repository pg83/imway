/* A fullscreen dmabuf candidate that opts into tearing: the perfect direct
 * scanout client whose flips may go async. Same dumb-buffer setup as the
 * scanout taint client, plus wp_tearing_control_v1. */
#include "wl_util.h"

#include <xf86drm.h>

#include <linux-dmabuf-v1-client-protocol.h>
#include <tearing-control-v1-client-protocol.h>

#include <sys/mman.h>

#define FOURCC_XRGB8888 0x34325258 /* 'XR24' */
#define W 1280
#define H 800

static struct zwp_linux_dmabuf_v1* dmabuf;
static struct wp_tearing_control_manager_v1* tearing_mgr;
static int linear_ok;
static struct wl_surface* surface;
static struct xdg_surface* xs;
static struct xdg_toplevel* tl;
static struct wl_buffer* buffer;
static int configured, mapped;

static void dmabuf_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t f) {
    (void)d; (void)z; (void)f;
}
static void dmabuf_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t fmt,
                            uint32_t hi, uint32_t lo) {
    (void)d; (void)z;
    if (fmt == FOURCC_XRGB8888 && hi == 0 && lo == 0) linear_ok = 1;
}
static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {dmabuf_format, dmabuf_modifier};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, 3);
    if (!strcmp(iface, wp_tearing_control_manager_v1_interface.name))
        tearing_mgr = wl_registry_bind(r, name, &wp_tearing_control_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_buffer* make_red_dumb(void) {
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        struct drm_mode_create_dumb create = {0};
        create.width = W;
        create.height = H;
        create.bpp = 32;
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
            close(fd);
            continue;
        }

        struct drm_mode_map_dumb map = {0};
        map.handle = create.handle;
        int prime = -1;
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0 ||
            drmPrimeHandleToFD(fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &prime) != 0 || prime < 0) {
            close(fd);
            continue;
        }

        uint32_t* px = mmap(NULL, (size_t)create.pitch * H, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, map.offset);
        if (px != MAP_FAILED) {
            for (size_t j = 0; j < (size_t)create.pitch * H / 4; j++) px[j] = 0xffff2020u;
            munmap(px, (size_t)create.pitch * H);
        }
        close(fd); /* the prime fd keeps the buffer alive */

        struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
        zwp_linux_buffer_params_v1_add(params, prime, 0, 0, create.pitch, 0, 0);
        close(prime);
        return zwp_linux_buffer_params_v1_create_immed(params, W, H, FOURCC_XRGB8888, 0);
    }
    return NULL;
}

/* async flips only kick in when a direct frame is already on the plane:
 * keep recommitting the same buffer so the flips keep coming */
static void frame_done(void* d, struct wl_callback* cb, uint32_t t);
static const struct wl_callback_listener frame_listener = {frame_done};

static void commit_frame(void) {
    struct wl_callback* cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &frame_listener, NULL);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, W, H);
    wl_surface_commit(surface);
}

static void frame_done(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d; (void)t;
    wl_callback_destroy(cb);
    commit_frame();
}

static void xs_configure(void* d, struct xdg_surface* s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    configured = 1;
    if (!mapped) {
        commit_frame();
        mapped = 1;
        printf("tearing candidate mapped\n");
    }
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* s) {
    (void)d; (void)t; (void)w; (void)h; (void)s;
}
static void tl_close(void* d, struct xdg_toplevel* t) { (void)d; (void)t; exit(0); }
static void tl_bounds(void* d, struct xdg_toplevel* t, int32_t w, int32_t h) {
    (void)d; (void)t; (void)w; (void)h;
}
static void tl_caps(void* d, struct xdg_toplevel* t, struct wl_array* c) { (void)d; (void)t; (void)c; }
static const struct xdg_toplevel_listener tl_listener = {tl_configure, tl_close, tl_bounds, tl_caps};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dmabuf || !tearing_mgr) return 77;
    zwp_linux_dmabuf_v1_add_listener(dmabuf, &dmabuf_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!linear_ok) return 77;

    buffer = make_red_dumb();
    if (!buffer) return 77;

    surface = wl_compositor_create_surface(wl_comp);
    struct wp_tearing_control_v1* tc =
        wp_tearing_control_manager_v1_get_tearing_control(tearing_mgr, surface);
    wp_tearing_control_v1_set_presentation_hint(tc, WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC);

    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "kms-tearing");
    xdg_toplevel_set_app_id(tl, "kms-tearing");
    xdg_toplevel_set_fullscreen(tl, NULL);
    wl_surface_commit(surface);

    while (wl_display_dispatch(wl_dpy) >= 0) {
    }

    return 0;
}
