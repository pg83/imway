// Fullscreen ARGB8888 dmabuf toplevel. An alpha-capable format may only go
// to direct scanout when the client declares the surface opaque: the primary
// plane does not blend, so a translucent buffer would render differently
// than in composition. Phase 1 commits without an opaque region (must not be
// a scanout candidate), phase 2 declares full opacity (must be one again).
// The buffer is a DRM dumb buffer from a card node; exits 77 without one.

#include "wl_util.h"

#include <xf86drm.h>

#include <linux-dmabuf-v1-client-protocol.h>

#include <sys/mman.h>

#define FOURCC_ARGB8888 0x34325241 /* 'AR24' */

static struct zwp_linux_dmabuf_v1* dmabuf;
static struct wl_surface* surface;
static int linear_ok, drawn, phase2;
static int32_t cw, ch;

static void dmabuf_format(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t fmt) {
    (void)d;
    (void)z;
    (void)fmt;
}
static void dmabuf_modifier(void* d, struct zwp_linux_dmabuf_v1* z, uint32_t fmt,
                            uint32_t hi, uint32_t lo) {
    (void)d;
    (void)z;
    if (fmt == FOURCC_ARGB8888 && hi == 0 && lo == 0) linear_ok = 1;
}
static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {dmabuf_format,
                                                                    dmabuf_modifier};

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, 3);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int make_dmabuf_fd(uint32_t* pitch) {
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        struct drm_mode_create_dumb create = {0};
        create.width = cw;
        create.height = ch;
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

        uint32_t* px = mmap(NULL, (size_t)create.pitch * ch, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, map.offset);
        if (px != MAP_FAILED) {
            for (size_t j = 0; j < (size_t)create.pitch * ch / 4; j++) px[j] = 0xFFFF8000u;
            munmap(px, (size_t)create.pitch * ch);
        }
        close(fd); /* the prime fd keeps the buffer alive */
        *pitch = create.pitch;
        return prime;
    }
    fprintf(stderr, "no usable /dev/dri/card node\n");
    exit(77);
}

static void draw(void) {
    uint32_t pitch = 0;
    int fd = make_dmabuf_fd(&pitch);

    struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, pitch, 0, 0); // LINEAR
    struct wl_buffer* buffer =
        zwp_linux_buffer_params_v1_create_immed(params, cw, ch, FOURCC_ARGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    close(fd);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, cw, ch);
    wl_surface_commit(surface);
    drawn = 1;
    printf("client_reg_scanout_opaque: phase1 %dx%d\n", cw, ch);
}

static void xs_configure(void* d, struct xdg_surface* xs, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(xs, serial);
    if (!drawn && cw > 0 && ch > 0) draw();
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void tl_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h,
                         struct wl_array* states) {
    (void)d;
    (void)t;
    (void)states;
    if (w > 0 && h > 0) {
        cw = w;
        ch = h;
    }
}
static void tl_close(void* d, struct xdg_toplevel* t) {
    (void)d;
    (void)t;
    exit(0);
}
static const struct xdg_toplevel_listener tl_listener = {
    .configure = tl_configure,
    .close = tl_close,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dmabuf) {
        fprintf(stderr, "no linux-dmabuf global\n");
        return 1;
    }
    zwp_linux_dmabuf_v1_add_listener(dmabuf, &dmabuf_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!linear_ok) {
        fprintf(stderr, "no ARGB8888+LINEAR\n");
        return 77;
    }

    wlk_watch_key = 30; // KEY_A: the scenario's phase-2 trigger

    surface = wl_compositor_create_surface(wl_comp);
    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "scanout-opaque");
    xdg_toplevel_set_app_id(tl, "scanout-opaque");
    xdg_toplevel_set_fullscreen(tl, NULL);
    wl_surface_commit(surface);

    for (;;) {
        if (drawn && !phase2 && wlk_watch_hits) {
            struct wl_region* region = wl_compositor_create_region(wl_comp);

            wl_region_add(region, 0, 0, cw, ch);
            wl_surface_set_opaque_region(surface, region);
            wl_region_destroy(region);
            wl_surface_commit(surface);
            wl_display_flush(wl_dpy);
            phase2 = 1;
            printf("client_reg_scanout_opaque: phase2\n");
        }

        if (wl_display_dispatch(wl_dpy) == -1) {
            break;
        }
    }
    return 0;
}
