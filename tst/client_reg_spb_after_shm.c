// A 200x200 blue toplevel with a subsurface at (50,50) that a viewport
// stretches to 100x100: first a 1x1 red wl_shm buffer, then, once the
// scenario's go-file releases it, a green single-pixel buffer of the same
// size in its place.

#include "wl_util.h"

#include <viewporter-client-protocol.h>
#include <single-pixel-buffer-v1-client-protocol.h>

static struct wp_single_pixel_buffer_manager_v1* sp_mgr;
static struct wp_viewporter* viewporter;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;

    if (!strcmp(iface, wp_single_pixel_buffer_manager_v1_interface.name)) {
        sp_mgr = wl_registry_bind(r, name, &wp_single_pixel_buffer_manager_v1_interface, 1);
    } else if (!strcmp(iface, wp_viewporter_interface.name)) {
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
    }
}

static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}

static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void wait_go(const char* name) {
    char path[512];

    snprintf(path, sizeof(path), "%s/go-%s", getenv("XDG_RUNTIME_DIR"), name);

    for (int i = 0; i < 1500; i++) {
        if (access(path, F_OK) == 0) {
            return;
        }

        usleep(20000);
        wl_display_roundtrip(wl_dpy);
    }

    fprintf(stderr, "the scenario never released %s\n", name);
    exit(1);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot()) {
        return 1;
    }

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!sp_mgr || !viewporter || !wl_subcomp) {
        fprintf(stderr, "no single-pixel buffers, viewporter or subcompositor\n");
        return 1;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "spb-after-shm", 200, 200, 0xff0000ffu);

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, child);
    struct wl_buffer* red = wl_solid(1, 1, 0xffff0000u);

    wl_subsurface_set_position(sub, 50, 50);
    wl_subsurface_set_desync(sub);
    wp_viewport_set_destination(vp, 100, 100);
    wl_surface_attach(child, red, 0, 0);
    wl_surface_damage_buffer(child, 0, 0, 1, 1);
    wl_surface_commit(child);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("red\n");
    wait_go("green");

    struct wl_buffer* green = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(sp_mgr, 0, 0xffffffffu, 0, 0xffffffffu);

    wl_surface_attach(child, green, 0, 0);
    wl_surface_damage_buffer(child, 0, 0, 1, 1);
    wl_surface_commit(child);
    wl_display_roundtrip(wl_dpy);
    printf("green\n");
    wait_go("done");
    wl_buffer_destroy(green);
    wl_buffer_destroy(red);
    wp_viewport_destroy(vp);
    wl_subsurface_destroy(sub);
    wl_surface_destroy(child);
    printf("spb after shm done\n");

    return 0;
}
