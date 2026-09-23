/* The fullscreen red dumb-buffer candidate of the scanout taint client,
 * which then, one KEY_A press at a time, does what composition alone can
 * show and takes it back, printing each step:
 *   "subsurface on"  a 32x32 blue subsurface over it
 *   "subsurface off" the subsurface destroyed
 *   "alpha on"       an alpha multiplier of one half
 *   "alpha off"      the multiplier back at full */
#define main taint_main
#include "client_kms_scanout_taint.c"
#undef main

#include <alpha-modifier-v1-client-protocol.h>

static struct wp_alpha_modifier_v1* alpha_mgr;

static void veto_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, wp_alpha_modifier_v1_interface.name))
        alpha_mgr = wl_registry_bind(r, name, &wp_alpha_modifier_v1_interface, 1);
}
static const struct wl_registry_listener veto_listener = {veto_global, extra_remove};

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
    if (!dmabuf || !alpha_mgr || !wl_subcomp) return 77;
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
    struct wl_surface* child = NULL;
    struct wl_subsurface* sub = NULL;
    int phase = 0;

    wlk_watch_key = 30; // KEY_A

    while (wl_display_dispatch(wl_dpy) >= 0) {
        while (wlk_watch_hits >= 2 * (phase + 1) && phase < 4) {
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
            }
        }
    }

    return 0;
}
