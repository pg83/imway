/* The fullscreen red dumb-buffer candidate of the scanout taint client,
 * which then, one KEY_A press at a time, does what composition alone can
 * show and takes it back, printing each step:
 *   "subsurface on"  a 32x32 blue subsurface over it
 *   "subsurface off" the subsurface destroyed
 *   "alpha on"       an alpha multiplier of one half
 *   "alpha off"      the multiplier back at full
 *   "color on"       a PQ BT.2020 image description on the surface
 *   "color off"      the description unset again
 *   "below on"       a 32x32 blue subsurface placed below it
 *   "below off"      that subsurface destroyed
 *   "short on"       a window geometry one row short of the buffer
 *   "short off"      the geometry back at the full buffer
 *   "turned on"      the buffer turned half a circle
 *   "turned off"     the buffer upright again
 *   "scaled on"      a buffer scale of 2, showing it at half size
 *   "scaled off"     the buffer scale back at 1
 *   "offset on"      the buffer attached one column right
 *   "offset off"     and back
 *   "cropped on"     a viewport showing the buffer's top left quarter
 *   "cropped off"    that viewport's source unset
 *   "shrunk on"      a viewport showing the buffer at half size
 *   "shrunk off"     that viewport's destination unset
 * and then the pointer's cursor, which the plane cannot always carry:
 *   "dmabuf cursor"  a 32x32 dma-buf cursor surface
 *   "tall cursor"    the cursor surface as 16x96 wl_shm stripes
 *   "no cursor"      the cursor hidden
 * A cursor step needs the pointer on the surface first ("pointer in"). */
#define main taint_main
#include "client_kms_scanout_taint.c"
#undef main

#include <alpha-modifier-v1-client-protocol.h>
#include <color-management-v1-client-protocol.h>
#include <viewporter-client-protocol.h>

static struct wp_alpha_modifier_v1* alpha_mgr;
static struct wp_color_manager_v1* color_mgr;
static struct wp_viewporter* viewporter;
static int desc_ready, desc_failed;

static void veto_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, wp_alpha_modifier_v1_interface.name))
        alpha_mgr = wl_registry_bind(r, name, &wp_alpha_modifier_v1_interface, 1);
    else if (!strcmp(iface, wp_color_manager_v1_interface.name))
        color_mgr = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
    else if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
}
static const struct wl_registry_listener veto_listener = {veto_global, extra_remove};

static void desc_ready_ev(void* d, struct wp_image_description_v1* i, uint32_t identity) {
    (void)d; (void)i; (void)identity;
    desc_ready = 1;
}
static void desc_ready2_ev(void* d, struct wp_image_description_v1* i, uint32_t hi, uint32_t lo) {
    (void)d; (void)i; (void)hi; (void)lo;
    desc_ready = 1;
}
static void desc_failed_ev(void* d, struct wp_image_description_v1* i, uint32_t cause, const char* msg) {
    (void)d; (void)i; (void)cause; (void)msg;
    desc_failed = 1;
}
static const struct wp_image_description_v1_listener desc_listener = {
    .failed = desc_failed_ev,
    .ready = desc_ready_ev,
    .ready2 = desc_ready2_ev,
};

// a small blue dumb buffer from a card node, as a LINEAR dma-buf
static struct wl_buffer* small_dumb(void) {
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        struct drm_mode_create_dumb create = {0};
        create.width = 32;
        create.height = 32;
        create.bpp = 32;
        int prime = -1;
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0 ||
            drmPrimeHandleToFD(fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &prime) != 0 || prime < 0) {
            close(fd);
            continue;
        }
        close(fd); /* the prime fd keeps the buffer alive */

        struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
        zwp_linux_buffer_params_v1_add(params, prime, 0, 0, create.pitch, 0, 0);
        close(prime);
        struct wl_buffer* b = zwp_linux_buffer_params_v1_create_immed(params, 32, 32, FOURCC_XRGB8888, 0);
        zwp_linux_buffer_params_v1_destroy(params);
        return b;
    }
    return NULL;
}

static void cursor_step(struct wl_surface* cs, struct wl_buffer* b, int w, int h, const char* what) {
    if (!wlp_enter_count) {
        fprintf(stderr, "%s: the pointer never entered the surface\n", what);
        exit(1);
    }
    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cs, 0, 0);
    wl_surface_attach(cs, b, 0, 0);
    wl_surface_damage(cs, 0, 0, w, h);
    wl_surface_commit(cs);
    wl_display_flush(wl_dpy);
    printf("%s\n", what);
}

static void reattach(int dx) {
    wl_surface_attach(surface, buffer, dx, 0);
    wl_surface_damage(surface, 0, 0, W, H);
}

static void step(const char* what) {
    wl_surface_commit(surface);
    wl_display_flush(wl_dpy);
    printf("%s\n", what);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &veto_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!dmabuf || !alpha_mgr || !wl_subcomp || !color_mgr || !viewporter) {
        fprintf(stderr, "SKIP: a global is missing\n");
        return 77;
    }
    wl_display_roundtrip(wl_dpy);
    if (!linear_ok) {
        fprintf(stderr, "SKIP: no LINEAR XRGB8888 dma-buf\n");
        return 77;
    }

    buffer = make_red_dumb();
    if (!buffer) {
        fprintf(stderr, "SKIP: no card node made the fullscreen dumb buffer\n");
        return 77;
    }

    struct wl_buffer* cursor_dmabuf = small_dumb();
    if (!cursor_dmabuf) {
        fprintf(stderr, "SKIP: no card node made the cursor dumb buffer\n");
        return 77;
    }

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "kms-taint");
    xdg_toplevel_set_app_id(tl, "kms-taint");
    xdg_toplevel_set_fullscreen(tl, NULL);
    wl_surface_commit(surface);

    struct wp_alpha_modifier_surface_v1* alpha = wp_alpha_modifier_v1_get_surface(alpha_mgr, surface);
    struct wp_color_management_surface_v1* cm = wp_color_manager_v1_get_surface(color_mgr, surface);
    struct wp_image_description_creator_params_v1* creator = wp_color_manager_v1_create_parametric_creator(color_mgr);

    wp_image_description_creator_params_v1_set_tf_named(creator, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    wp_image_description_creator_params_v1_set_primaries_named(creator, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);

    struct wp_image_description_v1* pq = wp_image_description_creator_params_v1_create(creator);

    wp_image_description_v1_add_listener(pq, &desc_listener, NULL);
    while (!desc_ready && !desc_failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!desc_ready) {
        fprintf(stderr, "the PQ image description is not ready\n");
        return 1;
    }
    struct wl_surface* child = NULL;
    struct wl_subsurface* sub = NULL;
    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    struct wp_viewport* viewport = wp_viewporter_get_viewport(viewporter, surface);
    int phase = 0;
    int pointer_in = 0;

    wlk_watch_key = 30; // KEY_A

    while (wl_display_dispatch(wl_dpy) >= 0) {
        if (wlp_enter_count && !pointer_in) {
            pointer_in = 1;
            printf("pointer in\n");
        }
        while (wlk_watch_hits >= 2 * (phase + 1) && phase < 23) {
            phase++;
            switch (phase) {
                case 1:
                    child = wl_compositor_create_surface(wl_comp);
                    sub = wl_subcompositor_get_subsurface(wl_subcomp, child, surface);
                    wl_subsurface_set_position(sub, 100, 100);
                    wl_surface_attach(child, wl_solid(32, 32, 0xff0000ff), 0, 0);
                    wl_surface_damage(child, 0, 0, 32, 32);
                    wl_surface_commit(child);
                    step("subsurface on");
                    break;
                case 2:
                    wl_subsurface_destroy(sub);
                    wl_surface_destroy(child);
                    step("subsurface off");
                    break;
                case 3:
                    wp_alpha_modifier_surface_v1_set_multiplier(alpha, 0x7fffffffu);
                    step("alpha on");
                    break;
                case 4:
                    wp_alpha_modifier_surface_v1_set_multiplier(alpha, 0xffffffffu);
                    step("alpha off");
                    break;
                case 5:
                    wp_color_management_surface_v1_set_image_description(cm, pq, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
                    step("color on");
                    break;
                case 6:
                    wp_color_management_surface_v1_unset_image_description(cm);
                    step("color off");
                    break;
                case 7:
                    child = wl_compositor_create_surface(wl_comp);
                    sub = wl_subcompositor_get_subsurface(wl_subcomp, child, surface);
                    wl_subsurface_place_below(sub, surface);
                    wl_surface_attach(child, wl_solid(32, 32, 0xff0000ff), 0, 0);
                    wl_surface_damage(child, 0, 0, 32, 32);
                    wl_surface_commit(child);
                    step("below on");
                    break;
                case 8:
                    wl_subsurface_destroy(sub);
                    wl_surface_destroy(child);
                    step("below off");
                    break;
                case 9:
                    xdg_surface_set_window_geometry(xs, 0, 0, W, H - 1);
                    step("short on");
                    break;
                case 10:
                    xdg_surface_set_window_geometry(xs, 0, 0, W, H);
                    step("short off");
                    break;
                case 11:
                    wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_180);
                    step("turned on");
                    break;
                case 12:
                    wl_surface_set_buffer_transform(surface, WL_OUTPUT_TRANSFORM_NORMAL);
                    step("turned off");
                    break;
                case 13:
                    wl_surface_set_buffer_scale(surface, 2);
                    step("scaled on");
                    break;
                case 14:
                    wl_surface_set_buffer_scale(surface, 1);
                    step("scaled off");
                    break;
                case 15:
                    reattach(1);
                    step("offset on");
                    break;
                case 16:
                    reattach(-1);
                    step("offset off");
                    break;
                case 17:
                    wp_viewport_set_source(viewport, 0, 0, wl_fixed_from_int(W / 2), wl_fixed_from_int(H / 2));
                    step("cropped on");
                    break;
                case 18:
                    wp_viewport_set_source(viewport, wl_fixed_from_int(-1), wl_fixed_from_int(-1), wl_fixed_from_int(-1), wl_fixed_from_int(-1));
                    step("cropped off");
                    break;
                case 19:
                    wp_viewport_set_destination(viewport, W / 2, H / 2);
                    step("shrunk on");
                    break;
                case 20:
                    wp_viewport_set_destination(viewport, -1, -1);
                    step("shrunk off");
                    break;
                case 21:
                    cursor_step(cursor, cursor_dmabuf, 32, 32, "dmabuf cursor");
                    break;
                case 22:
                    cursor_step(cursor, wl_solid(16, 96, 0xff0000ff), 16, 96, "tall cursor");
                    break;
                case 23:
                    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, NULL, 0, 0);
                    wl_display_flush(wl_dpy);
                    printf("no cursor\n");
                    break;
            }
        }
    }

    return 0;
}
