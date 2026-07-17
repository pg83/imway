// Regression: a nested subsurface leaves an output when any subsurface
// ancestor becomes unmapped, even if the nested surface retains its buffer.

#include "wl_util.h"

static struct wl_output* output;
static int child_enters, child_leaves;
static int grand_enters, grand_leaves;

struct membership {
    int* enters;
    int* leaves;
};

static void surface_enter(void* data, struct wl_surface* surface, struct wl_output* out) {
    (void)surface;
    struct membership* membership = data;
    if (out == output) (*membership->enters)++;
}

static void surface_leave(void* data, struct wl_surface* surface, struct wl_output* out) {
    (void)surface;
    struct membership* membership = data;
    if (out == output) (*membership->leaves)++;
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
};

static void output_geometry(void* data, struct wl_output* out, int32_t x, int32_t y,
                            int32_t pw, int32_t ph, int32_t subpixel,
                            const char* make, const char* model, int32_t transform) {
    (void)data; (void)out; (void)x; (void)y; (void)pw; (void)ph; (void)subpixel;
    (void)make; (void)model; (void)transform;
}
static void output_mode(void* data, struct wl_output* out, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh) {
    (void)data; (void)out; (void)flags; (void)width; (void)height; (void)refresh;
}
static void output_done(void* data, struct wl_output* out) { (void)data; (void)out; }
static void output_scale(void* data, struct wl_output* out, int32_t scale) {
    (void)data; (void)out; (void)scale;
}
static void output_name(void* data, struct wl_output* out, const char* name) {
    (void)data; (void)out; (void)name;
}
static void output_description(void* data, struct wl_output* out, const char* description) {
    (void)data; (void)out; (void)description;
}
static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data;
    if (!strcmp(interface, wl_output_interface.name)) {
        output = wl_registry_bind(registry, name, &wl_output_interface,
                                  version < 4 ? version : 4);
        wl_output_add_listener(output, &output_listener, NULL);
    }
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
    if (wl_boot() || !wl_subcomp) return 1;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!output) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "surface-output-ancestor", 360, 240, 0xFFFF0000);

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_surface* grand = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* child_sub =
        wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    struct wl_subsurface* grand_sub =
        wl_subcompositor_get_subsurface(wl_subcomp, grand, child);
    (void)grand_sub;
    wl_subsurface_set_position(child_sub, 30, 30);

    struct membership child_membership = {&child_enters, &child_leaves};
    struct membership grand_membership = {&grand_enters, &grand_leaves};
    wl_surface_add_listener(child, &surface_listener, &child_membership);
    wl_surface_add_listener(grand, &surface_listener, &grand_membership);

    wl_surface_attach(child, wl_solid(180, 140, 0xFF00FF00), 0, 0);
    wl_surface_damage(child, 0, 0, 180, 140);
    wl_surface_commit(child);
    wl_surface_attach(grand, wl_solid(80, 60, 0xFF0000FF), 0, 0);
    wl_surface_damage(grand, 0, 0, 80, 60);
    wl_surface_commit(grand);
    wl_surface_commit(top.surface);

    for (int i = 0; i < 300 && (!child_enters || !grand_enters); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(10000);
    }
    if (child_enters != 1 || grand_enters != 1) {
        fprintf(stderr, "initial output enters child=%d grand=%d\n",
                child_enters, grand_enters);
        return 1;
    }

    wl_surface_attach(child, NULL, 0, 0);
    wl_surface_commit(child);
    wl_surface_commit(top.surface);

    for (int i = 0; i < 300 && (!child_leaves || !grand_leaves); i++) {
        if (wl_display_roundtrip(wl_dpy) < 0) break;
        usleep(10000);
    }

    if (child_leaves != 1 || grand_leaves != 1) {
        fprintf(stderr, "ancestor unmap leaves child=%d grand=%d\n",
                child_leaves, grand_leaves);
        return 1;
    }

    printf("surface output ancestor ok\n");
    return 0;
}
