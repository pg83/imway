// Green toplevel whose pointer cursor is a 32x32 surface of red/blue stripes
// the hardware cursor plane cannot take as it is, set on the first enter:
//   dmabuf  the stripes in a card node's dumb buffer as a dma-buf
//           ("cursor-set"); exits 77 without one or without LINEAR XRGB8888
//   sigbus  the stripes in a wl_shm pool whose memfd is cut to nothing
//           before the commit ("cursor-set"); then waits for the protocol
//           error and exits 0 only on wl_shm invalid_fd
// or, as 64x64 wl_shm stripes four rows wide, a cursor surface the plane
// would show wrong from its raw pixels:
//   scaled    at buffer scale 2
//   turned    under a 90 degree buffer transform
//   shrunk    through a viewport destination of 48x48
//   straight  with straight (not premultiplied) alpha

#include "wl_util.h"

#include <linux-dmabuf-v1-client-protocol.h>
#include <viewporter-client-protocol.h>
#include <color-representation-v1-client-protocol.h>

#include <xf86drm.h>

#define FOURCC_XRGB8888 0x34325258 /* 'XR24' */
#define CW 32
#define CH 32

static struct zwp_linux_dmabuf_v1* dmabuf;
static struct wp_viewporter* viewporter;
static struct wp_color_representation_manager_v1* representation;
static int linear_ok;

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
    if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name)) {
        dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, 3);
        zwp_linux_dmabuf_v1_add_listener(dmabuf, &dmabuf_listener, NULL);
    } else if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
    else if (!strcmp(iface, wp_color_representation_manager_v1_interface.name))
        representation = wl_registry_bind(r, name, &wp_color_representation_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void fill_stripes(int fd, size_t size) {
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++)
            px[y * CW + x] = (y & 1) ? 0xFF0000FFu : 0xFFFF0000u;
    munmap(px, size);
}

// a dumb buffer from a card node, exported as a LINEAR dma-buf
static struct wl_buffer* dmabuf_stripes(void) {
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        struct drm_mode_create_dumb create = {0};
        create.width = CW;
        create.height = CH;
        create.bpp = 32;
        struct drm_mode_map_dumb map = {0};
        int prime = -1;
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
            close(fd);
            continue;
        }
        map.handle = create.handle;
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0 ||
            drmPrimeHandleToFD(fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &prime) != 0 || prime < 0) {
            close(fd);
            continue;
        }

        uint32_t* px = mmap(NULL, (size_t)create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
        if (px == MAP_FAILED) {
            close(prime);
            close(fd);
            continue;
        }
        for (int y = 0; y < CH; y++)
            for (int x = 0; x < CW; x++)
                px[y * (create.pitch / 4) + x] = (y & 1) ? 0xFF0000FFu : 0xFFFF0000u;
        munmap(px, (size_t)create.size);
        close(fd); /* the prime fd keeps the buffer alive */

        struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
        zwp_linux_buffer_params_v1_add(params, prime, 0, 0, create.pitch, 0, 0);
        struct wl_buffer* buf = zwp_linux_buffer_params_v1_create_immed(params, CW, CH, FOURCC_XRGB8888, 0);
        zwp_linux_buffer_params_v1_destroy(params);
        close(prime);
        return buf;
    }
    fprintf(stderr, "client_kms_cursor_edges: no card node gives dumb-buffer dma-bufs\n");
    exit(77);
}

// the pool maps the full stripes; the file behind it then shrinks to
// nothing, so any read through that mapping faults
static struct wl_buffer* truncated_stripes(void) {
    size_t size = (size_t)CW * CH * 4;
    int fd = memfd_create("cursor-sigbus", 0);
    if (fd < 0 || ftruncate(fd, (off_t)size) < 0) {
        perror("memfd");
        exit(1);
    }
    fill_stripes(fd, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, (int32_t)size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, CW, CH, CW * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    wl_display_roundtrip(wl_dpy);
    if (ftruncate(fd, 0) < 0) {
        perror("ftruncate");
        exit(1);
    }
    close(fd);
    return buf;
}

// 64x64 red/blue stripes, four rows each, in a plain wl_shm pool
static struct wl_buffer* wide_stripes(void) {
    int w = 64, h = 64, stride = w * 4, size = stride * h;
    int fd = memfd_create("cursor-wide", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }

    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (px == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            px[y * w + x] = (y / 4 & 1) ? 0xFF0000FFu : 0xFFFF0000u;
        }
    }

    munmap(px, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buf;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (argc != 2 || wl_boot()) return 2;

    int sigbus = !strcmp(argv[1], "sigbus");
    int wide = !strcmp(argv[1], "scaled") || !strcmp(argv[1], "turned") || !strcmp(argv[1], "shrunk") || !strcmp(argv[1], "straight");
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (wide && (!viewporter || !representation)) {
        fprintf(stderr, "no wp_viewporter or wp_color_representation_manager_v1\n");
        return 1;
    }

    if (!sigbus && !wide) {
        if (!dmabuf) return 77;
        wl_display_roundtrip(wl_dpy);
        if (!linear_ok) return 77;
    }

    struct wl_buffer* stripes = wide ? wide_stripes() : sigbus ? truncated_stripes() : dmabuf_stripes();
    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "kms-cursor-edges", 400, 300, 0xFF00FF00u);
    printf("client_kms_cursor_edges: mapped\n");

    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    struct wp_viewport* viewport = NULL;
    struct wp_color_representation_surface_v1* repr = NULL;
    int set = 0;

    if (!strcmp(argv[1], "scaled")) {
        wl_surface_set_buffer_scale(cursor, 2);
    } else if (!strcmp(argv[1], "turned")) {
        wl_surface_set_buffer_transform(cursor, WL_OUTPUT_TRANSFORM_90);
    } else if (!strcmp(argv[1], "shrunk")) {
        viewport = wp_viewporter_get_viewport(viewporter, cursor);
        wp_viewport_set_destination(viewport, 48, 48);
    } else if (!strcmp(argv[1], "straight")) {
        repr = wp_color_representation_manager_v1_get_surface(representation, cursor);
        wp_color_representation_surface_v1_set_alpha_mode(repr, WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT);
    }

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (wlp_enter_count && !set && wl_ptr) {
            wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor, 0, 0);
            wl_surface_attach(cursor, stripes, 0, 0);
            wl_surface_damage_buffer(cursor, 0, 0, 64, 64);
            wl_surface_commit(cursor);
            wl_display_flush(wl_dpy);
            set = 1;
            printf("client_kms_cursor_edges: cursor-set\n");
        }
    }

    if (repr) {
        wp_color_representation_surface_v1_destroy(repr);
    }

    if (viewport) {
        wp_viewport_destroy(viewport);
    }

    if (!sigbus) return 1;

    const struct wl_interface* iface = NULL;
    uint32_t code = wl_display_get_protocol_error(wl_dpy, &iface, NULL);

    if (!iface || strcmp(iface->name, "wl_buffer") || code != WL_SHM_ERROR_INVALID_FD) {
        fprintf(stderr, "no wl_shm invalid_fd: %s code %u\n", iface ? iface->name : "?", code);
        return 1;
    }

    printf("client_kms_cursor_edges: invalid-fd\n");
    return 0;
}
