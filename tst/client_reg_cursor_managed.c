// A client cursor surface tagged with a PQ image description. The hardware
// cursor plane copies raw buffer bytes and cannot apply color management, so
// a managed cursor must fall back to composition (where it shows up in
// screenshots; the fake cursor plane does not).

#include "wl_util.h"
#include <color-management-v1-client-protocol.h>

static struct wp_color_manager_v1* color_mgr;
static struct wl_surface* cursor_surface;
static int desc_ready, desc_failed, cursor_set;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, wp_color_manager_v1_interface.name))
        color_mgr = wl_registry_bind(r, name, &wp_color_manager_v1_interface,
                                     v < 3 ? v : 3);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void desc_ready_ev(void* d, struct wp_image_description_v1* i, uint32_t identity) {
    (void)d;
    (void)i;
    (void)identity;
    desc_ready = 1;
}
static void desc_ready2_ev(void* d, struct wp_image_description_v1* i,
                           uint32_t hi, uint32_t lo) {
    (void)d;
    (void)i;
    (void)hi;
    (void)lo;
    desc_ready = 1;
}
static void desc_failed_ev(void* d, struct wp_image_description_v1* i,
                           uint32_t cause, const char* msg) {
    (void)d;
    (void)i;
    (void)cause;
    (void)msg;
    desc_failed = 1;
}
static const struct wp_image_description_v1_listener desc_listener = {
    .failed = desc_failed_ev,
    .ready = desc_ready_ev,
    .ready2 = desc_ready2_ev,
};

// premultiplied magenta at half code: PQ-decodes to ~94 nits, well inside
// the SDR range, so it survives display mapping recognizably
static struct wl_buffer* magenta(int w, int h) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("cursor-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) px[i] = 0xFF800080u;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf =
        wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!color_mgr) {
        fprintf(stderr, "no color manager\n");
        return 1;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "cursor-managed", 400, 300, 0xFF00FF00u);
    printf("client_reg_cursor_managed: mapped\n");

    cursor_surface = wl_compositor_create_surface(wl_comp);

    struct wp_color_management_surface_v1* cm =
        wp_color_manager_v1_get_surface(color_mgr, cursor_surface);
    struct wp_image_description_creator_params_v1* creator =
        wp_color_manager_v1_create_parametric_creator(color_mgr);

    wp_image_description_creator_params_v1_set_tf_named(
        creator, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    wp_image_description_creator_params_v1_set_primaries_named(
        creator, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);

    struct wp_image_description_v1* desc =
        wp_image_description_creator_params_v1_create(creator);

    wp_image_description_v1_add_listener(desc, &desc_listener, NULL);

    while (!desc_ready && !desc_failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!desc_ready) {
        fprintf(stderr, "image description not ready\n");
        return 1;
    }
    printf("client_reg_cursor_managed: desc-ready\n");

    for (;;) {
        // the pointer may have entered while earlier loops were dispatching:
        // check before blocking in dispatch
        if (wlp_enter_count && !cursor_set && wl_ptr) {
            wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor_surface, 0, 0);
            wp_color_management_surface_v1_set_image_description(
                cm, desc, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
            wl_surface_attach(cursor_surface, magenta(32, 32), 0, 0);
            wl_surface_damage(cursor_surface, 0, 0, 32, 32);
            wl_surface_commit(cursor_surface);
            wl_display_flush(wl_dpy);
            cursor_set = 1;
            printf("client_reg_cursor_managed: cursor-set\n");
        }

        if (wl_display_dispatch(wl_dpy) == -1) {
            break;
        }
    }
    return 0;
}
