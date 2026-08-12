// Regression: Wayland ARGB buffers are premultiplied in the electrical
// domain. Exercise a translucent subsurface first as legacy sRGB and then as
// PQ/BT.2020; the headless scenario checks the actual composed signal.

#include "wl_util.h"
#include <color-management-v1-client-protocol.h>

static struct wp_color_manager_v1* cm;
static int desc_ready;

static void desc_failed(void* data, struct wp_image_description_v1* desc,
                        uint32_t cause, const char* message) {
    (void)data;
    (void)desc;
    fprintf(stderr, "image description failed: %u %s\n", cause, message);
}

static void desc_ready_event(void* data, struct wp_image_description_v1* desc,
                             uint32_t identity) {
    (void)data;
    (void)desc;
    (void)identity;
    desc_ready = 1;
}

static const struct wp_image_description_v1_listener desc_listener = {
    desc_failed,
    desc_ready_event,
};

static void registry_global(void* data, struct wl_registry* registry,
                            uint32_t name, const char* interface, uint32_t version) {
    (void)data;
    (void)version;

    if (!strcmp(interface, wp_color_manager_v1_interface.name)) {
        cm = wl_registry_bind(registry, name, &wp_color_manager_v1_interface, 1);
    }
}

static void registry_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove,
};

static void pump(int milliseconds) {
    for (int i = 0; i < milliseconds / 20; i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(20000);
    }
}

static struct wl_buffer* striped_buffer(int pq) {
    static const uint8_t alpha[] = {0, 64, 128, 255};
    static const uint8_t sdr_rgb[] = {220, 100, 40};
    static const uint8_t pq_rgb[] = {200, 160, 100};
    const uint8_t* straight = pq ? pq_rgb : sdr_rgb;
    int width = 200, height = 100, stride = width * 4, size = stride * height;
    int fd = memfd_create("alpha-stripes", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }

    uint32_t* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t a = alpha[x / 50];
            uint8_t r = (uint8_t)((straight[0] * a + 127) / 255);
            uint8_t g = (uint8_t)((straight[1] * a + 127) / 255);
            uint8_t b = (uint8_t)((straight[2] * a + 127) / 255);

            pixels[y * width + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                                          ((uint32_t)g << 8) | b;
        }
    }

    munmap(pixels, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buffer;
}

static void attach_stripes(struct wl_surface* surface, int pq) {
    wl_surface_attach(surface, striped_buffer(pq), 0, 0);
    wl_surface_damage(surface, 0, 0, 200, 100);
    wl_surface_commit(surface);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (wl_boot()) {
        return 1;
    }

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!cm) {
        fprintf(stderr, "no color manager\n");
        return 1;
    }

    struct wl_toplevel_ctx top = {};

    wl_make_toplevel(&top, "color-alpha", 300, 200, 0xff204080);

    struct wl_surface* overlay = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* subsurface =
        wl_subcompositor_get_subsurface(wl_subcomp, overlay, top.surface);

    wl_subsurface_set_position(subsurface, 50, 50);
    attach_stripes(overlay, 0);
    wl_surface_commit(top.surface);
    printf("client_reg_color_alpha: sdr-alpha\n");
    pump(1000);

    struct wp_image_description_creator_params_v1* params =
        wp_color_manager_v1_create_parametric_creator(cm);

    wp_image_description_creator_params_v1_set_tf_named(
        params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    wp_image_description_creator_params_v1_set_primaries_named(
        params, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);

    struct wp_image_description_v1* desc =
        wp_image_description_creator_params_v1_create(params);

    wp_image_description_v1_add_listener(desc, &desc_listener, NULL);

    for (int i = 0; i < 100 && !desc_ready; i++) {
        wl_display_roundtrip(wl_dpy);
        usleep(20000);
    }

    if (!desc_ready) {
        fprintf(stderr, "image description not ready\n");
        return 1;
    }

    struct wp_color_management_surface_v1* top_cm =
        wp_color_manager_v1_get_surface(cm, top.surface);
    struct wp_color_management_surface_v1* overlay_cm =
        wp_color_manager_v1_get_surface(cm, overlay);

    wp_color_management_surface_v1_set_image_description(
        top_cm, desc, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
    wp_color_management_surface_v1_set_image_description(
        overlay_cm, desc, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);

    // Opaque base PQ=(80,100,120); the stripes contain the same straight
    // electrical PQ color at alpha 0, 64, 128 and 255.
    wl_surface_attach(top.surface, wl_solid(300, 200, 0xff506478), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 300, 200);
    attach_stripes(overlay, 1);
    wl_surface_commit(top.surface);
    printf("client_reg_color_alpha: pq-alpha\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
