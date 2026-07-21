#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <amdgpu.h>
#include <amdgpu_drm.h>
#include <drm_fourcc.h>
#include <xf86drm.h>

#include <color-representation-v1-client-protocol.h>
#include <linux-dmabuf-v1-client-protocol.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

#define W 512
#define H 256

static struct wl_compositor* compositor;
static struct xdg_wm_base* wm_base;
static struct zwp_linux_dmabuf_v1* dmabuf;
static struct wp_color_representation_manager_v1* representation_manager;
static struct wl_surface* surface;
static int nv12_linear;
static int drawn;

static void dmabuf_format(void* data, struct zwp_linux_dmabuf_v1* object, uint32_t format) {
    (void)data;
    (void)object;
    (void)format;
}

static void dmabuf_modifier(void* data, struct zwp_linux_dmabuf_v1* object,
                            uint32_t format, uint32_t modifier_hi,
                            uint32_t modifier_lo) {
    (void)data;
    (void)object;
    if (format == DRM_FORMAT_NV12 && !modifier_hi && !modifier_lo) nv12_linear = 1;
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    .format = dmabuf_format,
    .modifier = dmabuf_modifier,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data;
    (void)version;
    if (!strcmp(interface, wl_compositor_interface.name))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(interface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, 3);
    else if (!strcmp(interface, wp_color_representation_manager_v1_interface.name))
        representation_manager = wl_registry_bind(
            registry, name, &wp_color_representation_manager_v1_interface, 1);
}

static void registry_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static void wm_base_ping(void* data, struct xdg_wm_base* object, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(object, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {.ping = wm_base_ping};

static int make_nv12(void) {
    int drm_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) return -1;

    uint32_t major = 0, minor = 0;
    amdgpu_device_handle device = NULL;
    if (amdgpu_device_initialize(drm_fd, &major, &minor, &device)) {
        close(drm_fd);
        return -1;
    }

    struct amdgpu_bo_alloc_request request = {
        .alloc_size = W * H * 3 / 2,
        .phys_alignment = 4096,
        .preferred_heap = AMDGPU_GEM_DOMAIN_VRAM,
        .flags = AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
    };
    amdgpu_bo_handle bo = NULL;
    void* map = NULL;
    uint32_t exported = 0;

    if (amdgpu_bo_alloc(device, &request, &bo) || amdgpu_bo_cpu_map(bo, &map)) {
        if (bo) amdgpu_bo_free(bo);
        amdgpu_device_deinitialize(device);
        close(drm_fd);
        return -1;
    }

    // BT.709 limited-range red: Y=63, Cb=102, Cr=240.
    memset(map, 63, W * H);
    uint8_t* uv = (uint8_t*)map + W * H;
    for (int i = 0; i < W * H / 4; i++) {
        uv[i * 2] = 102;
        uv[i * 2 + 1] = 240;
    }

    amdgpu_bo_cpu_unmap(bo);
    int rc = amdgpu_bo_export(bo, amdgpu_bo_handle_type_dma_buf_fd, &exported);
    amdgpu_bo_free(bo);
    amdgpu_device_deinitialize(device);
    close(drm_fd);
    return rc ? -1 : (int)exported;
}

static void draw(void) {
    int fd = make_nv12();
    if (fd < 0) exit(77);

    struct zwp_linux_buffer_params_v1* params =
        zwp_linux_dmabuf_v1_create_params(dmabuf);
    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, W, 0, 0);
    zwp_linux_buffer_params_v1_add(params, fd, 1, W * H, W, 0, 0);
    struct wl_buffer* buffer = zwp_linux_buffer_params_v1_create_immed(
        params, W, H, DRM_FORMAT_NV12, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    close(fd);

    struct wp_color_representation_surface_v1* representation =
        wp_color_representation_manager_v1_get_surface(representation_manager, surface);
    wp_color_representation_surface_v1_set_coefficients_and_range(
        representation, WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709,
        WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED);
    wp_color_representation_surface_v1_set_chroma_location(
        representation, WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_0);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, W, H);
    wl_surface_commit(surface);
    wl_buffer_destroy(buffer);
    drawn = 1;
    puts("client_reg_yuv_dmabuf: mapped NV12");
}

static void xdg_surface_configure(void* data, struct xdg_surface* object,
                                  uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(object, serial);
    if (!drawn) draw();
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void* data, struct xdg_toplevel* object,
                               int32_t width, int32_t height,
                               struct wl_array* states) {
    (void)data;
    (void)object;
    (void)width;
    (void)height;
    (void)states;
}

static void toplevel_close(void* data, struct xdg_toplevel* object) {
    (void)data;
    (void)object;
    exit(0);
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    struct wl_display* display = wl_display_connect(NULL);
    if (!display) return 1;

    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !wm_base || !dmabuf || !representation_manager) return 1;

    zwp_linux_dmabuf_v1_add_listener(dmabuf, &dmabuf_listener, NULL);
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
    wl_display_roundtrip(display);
    if (!nv12_linear) {
        fputs("client_reg_yuv_dmabuf: NV12+LINEAR not advertised\n", stderr);
        return 1;
    }

    surface = wl_compositor_create_surface(compositor);
    struct xdg_surface* xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    struct xdg_toplevel* toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
    xdg_toplevel_set_title(toplevel, "client_reg_yuv_dmabuf");
    wl_surface_commit(surface);

    while (wl_display_dispatch(display) != -1) {}
    return 0;
}
