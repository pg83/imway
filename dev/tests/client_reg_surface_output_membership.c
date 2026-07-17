// Regression: a mapped wl_surface enters the compositor output, and leaves it
// again when the surface is unmapped.

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_output* output;
static struct wl_surface* surface;
static struct xdg_surface* xdg_surface;
static int entered;
static int left;
static int configured;

static void surface_enter(void* data, struct wl_surface* target, struct wl_output* out) {
    (void)data; (void)target;
    if (out == output) {
        entered++;
        printf("surface entered output\n");
    }
}

static void surface_leave(void* data, struct wl_surface* target, struct wl_output* out) {
    (void)data; (void)target;
    if (out == output) {
        left++;
        printf("surface left output\n");
    }
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
};

static void xdg_configure(void* data, struct xdg_surface* target, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(target, serial);

    if (!configured) {
        configured = 1;
        wl_surface_attach(surface, wl_solid(240, 160, 0xFFFF0000), 0, 0);
        wl_surface_damage(surface, 0, 0, 240, 160);
        wl_surface_commit(surface);
    }
}

static const struct xdg_surface_listener xdg_listener = {
    xdg_configure,
};

static void toplevel_configure(void* data, struct xdg_toplevel* toplevel,
                               int32_t width, int32_t height, struct wl_array* states) {
    (void)data; (void)toplevel; (void)width; (void)height; (void)states;
}

static void toplevel_close(void* data, struct xdg_toplevel* toplevel) {
    (void)data; (void)toplevel;
    exit(0);
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure, toplevel_close,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, wl_output_interface.name))
        output = wl_registry_bind(registry, name, &wl_output_interface, 4);
}

static void registry_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global, registry_remove,
};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 1;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!output) {
        fprintf(stderr, "no output\n");
        return 1;
    }

    surface = wl_compositor_create_surface(wl_comp);
    wl_surface_add_listener(surface, &surface_listener, NULL);
    xdg_surface = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_listener, NULL);
    struct xdg_toplevel* toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
    xdg_toplevel_set_title(toplevel, "surface-output-membership");
    xdg_toplevel_set_app_id(toplevel, "surface-output-membership");
    wl_surface_commit(surface);
    printf("output membership ready\n");

    while (!entered && wl_display_dispatch(wl_dpy) != -1) {
    }

    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_surface_attach(surface, NULL, 0, 0);
    wl_surface_commit(surface);

    while (!left && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (entered != 1 || left != 1) {
        fprintf(stderr, "output membership enter=%d leave=%d\n", entered, left);
        return 1;
    }

    printf("surface output membership ok\n");
    return 0;
}
