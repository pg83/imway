// wl_pointer across seat versions, and pointers of other clients. One
// client holds pointers from a v4, a v5 and a v8 seat; a second connection
// holds a pointer of its own. The scenario scrolls by half a notch on each
// axis, finger-scrolls, stops both axes and presses two buttons on top of
// each other. v4 has no frames, sources, discrete steps or stops; v5 gets
// sources and stops but no discrete step for a half notch; v8 gets no
// value120 for it either; the other client hears nothing at all.

#include "wl_util.h"

struct counts {
    int enters, frames, axes, sources, stops, discretes, value120s, buttons;
};

static struct counts c4, c5, c8, other;

static void p_enter(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* su, wl_fixed_t x, wl_fixed_t y) {
    (void)p; (void)s; (void)su; (void)x; (void)y;
    ((struct counts*)d)->enters++;
}
static void p_leave(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* su) { (void)d; (void)p; (void)s; (void)su; }
static void p_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) { (void)d; (void)p; (void)t; (void)x; (void)y; }
static void p_button(void* d, struct wl_pointer* p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) {
    (void)p; (void)s; (void)t; (void)b; (void)st;
    ((struct counts*)d)->buttons++;
}
static void p_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t a, wl_fixed_t v) {
    (void)p; (void)t; (void)a; (void)v;
    ((struct counts*)d)->axes++;
}
static void p_frame(void* d, struct wl_pointer* p) {
    (void)p;
    ((struct counts*)d)->frames++;
}
static void p_source(void* d, struct wl_pointer* p, uint32_t s) {
    (void)p; (void)s;
    ((struct counts*)d)->sources++;
}
static void p_stop(void* d, struct wl_pointer* p, uint32_t t, uint32_t a) {
    (void)p; (void)t; (void)a;
    ((struct counts*)d)->stops++;
}
static void p_discrete(void* d, struct wl_pointer* p, uint32_t a, int32_t v) {
    (void)p; (void)a; (void)v;
    ((struct counts*)d)->discretes++;
}
static void p_value120(void* d, struct wl_pointer* p, uint32_t a, int32_t v) {
    (void)p; (void)a; (void)v;
    ((struct counts*)d)->value120s++;
}
static void p_reldir(void* d, struct wl_pointer* p, uint32_t a, uint32_t dir) { (void)d; (void)p; (void)a; (void)dir; }
static const struct wl_pointer_listener counting = {
    .enter = p_enter,
    .leave = p_leave,
    .motion = p_motion,
    .button = p_button,
    .axis = p_axis,
    .frame = p_frame,
    .axis_source = p_source,
    .axis_stop = p_stop,
    .axis_discrete = p_discrete,
    .axis_value120 = p_value120,
    .axis_relative_direction = p_reldir,
};

static uint32_t seat_name;

static void reg_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)r; (void)v;
    if (!strcmp(iface, wl_seat_interface.name) && !seat_name)
        seat_name = name;
}
static void reg_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = {reg_global, reg_remove};

static void pointer_at(struct wl_display* dpy, uint32_t version, struct counts* into) {
    struct wl_registry* reg = wl_display_get_registry(dpy);

    seat_name = 0;
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);

    if (!seat_name) exit(2);

    struct wl_seat* seat = wl_registry_bind(reg, seat_name, &wl_seat_interface, version);

    wl_pointer_add_listener(wl_seat_get_pointer(seat), &counting, into);
    wl_display_roundtrip(dpy);
}

static int check(const char* what, const struct counts* c, int frames, int sources, int stops, int discretes, int value120s) {
    printf("%s: enters=%d frames=%d axes=%d sources=%d stops=%d discretes=%d value120=%d buttons=%d\n", what, c->enters,
           c->frames, c->axes, c->sources, c->stops, c->discretes, c->value120s, c->buttons);

    if ((frames ? !c->frames : c->frames) || (sources ? !c->sources : c->sources) || c->stops != stops ||
        c->discretes != discretes || c->value120s != value120s || c->axes != 3 || c->buttons != 4) {
        fprintf(stderr, "%s: unexpected pointer events\n", what);
        return 1;
    }

    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    if (wl_boot()) return 2;

    struct wl_display* bystander = wl_display_connect(NULL);

    if (!bystander) return 2;

    pointer_at(bystander, 8, &other);
    pointer_at(wl_dpy, 4, &c4);
    pointer_at(wl_dpy, 5, &c5);
    pointer_at(wl_dpy, 8, &c8);

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "pointer-versions", 300, 200, 0xFF30A0A0u);
    printf("client_reg_pointer_versions: mapped\n");

    while (!(c4.enters && c5.enters && c8.enters) && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("pointer entered\n");

    while (!(c4.buttons == 4 && c5.buttons == 4 && c8.buttons == 4) && wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_display_roundtrip(bystander);

    if (check("v4", &c4, 0, 0, 0, 0, 0) || check("v5", &c5, 1, 1, 2, 0, 0) || check("v8", &c8, 1, 1, 2, 0, 0)) {
        return 1;
    }

    if (other.enters || other.axes || other.buttons || other.frames) {
        fprintf(stderr, "the other client heard the pointer\n");
        return 1;
    }

    printf("pointer versions done\n");
    wl_display_disconnect(bystander);

    return 0;
}
