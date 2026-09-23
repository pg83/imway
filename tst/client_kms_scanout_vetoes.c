/* The fullscreen red dumb-buffer candidate of the scanout taint client,
 * which then, one KEY_A press at a time, does what composition alone can
 * show and takes it back, printing each step:
 *   "subsurface on"  a 32x32 blue subsurface over it
 *   "subsurface off" the subsurface destroyed
 *   "alpha on"       an alpha multiplier of one half
 *   "alpha off"      the multiplier back at full
 *   "color on"       a PQ BT.2020 image description on the surface
 *   "color off"      the description unset again */
#define main taint_main
#include "client_kms_scanout_taint.c"
#undef main

#include <alpha-modifier-v1-client-protocol.h>
#include <color-management-v1-client-protocol.h>

static struct wp_alpha_modifier_v1* alpha_mgr;
static struct wp_color_manager_v1* color_mgr;
static int desc_ready, desc_failed;

static void veto_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, wp_alpha_modifier_v1_interface.name))
        alpha_mgr = wl_registry_bind(r, name, &wp_alpha_modifier_v1_interface, 1);
    else if (!strcmp(iface, wp_color_manager_v1_interface.name))
        color_mgr = wl_registry_bind(r, name, &wp_color_manager_v1_interface, 1);
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
    if (!dmabuf || !alpha_mgr || !wl_subcomp || !color_mgr) return 77;
    zwp_linux_dmabuf_v1_add_listener(dmabuf, &dmabuf_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!linear_ok) return 77;

    buffer = make_red_dumb();
    if (!buffer) return 77;

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
    int phase = 0;

    wlk_watch_key = 30; // KEY_A

    while (wl_display_dispatch(wl_dpy) >= 0) {
        while (wlk_watch_hits >= 2 * (phase + 1) && phase < 6) {
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
            }
        }
    }

    return 0;
}
