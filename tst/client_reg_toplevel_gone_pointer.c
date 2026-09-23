// The pointer on a window whose xdg_toplevel is destroyed (the wl_surface
// lives on, role-less): the surface is no longer shown, so the pointer
// leaves it right away, not at whatever input comes next.

#include "wl_util.h"

static int leaves;

static void p_enter(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* su, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)x; (void)y;
    wlp_enter_count++;
    wlp_enter_serial = s;
    wlp_focus = su;
}
static void p_leave(void* d, struct wl_pointer* p, uint32_t s, struct wl_surface* su) {
    (void)d; (void)p; (void)s; (void)su;
    leaves++;
    wlp_focus = NULL;
}
static void p_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)t; (void)x; (void)y;
}
static void p_button(void* d, struct wl_pointer* p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) {
    (void)d; (void)p; (void)s; (void)t; (void)b; (void)st;
}
static void p_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t a, wl_fixed_t v) {
    (void)d; (void)p; (void)t; (void)a; (void)v;
}
static void p_frame(void* d, struct wl_pointer* p) { (void)d; (void)p; }
static void p_source(void* d, struct wl_pointer* p, uint32_t s) { (void)d; (void)p; (void)s; }
static void p_stop(void* d, struct wl_pointer* p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void p_discrete(void* d, struct wl_pointer* p, uint32_t a, int32_t v) { (void)d; (void)p; (void)a; (void)v; }
static const struct wl_pointer_listener listener = {
    .enter = p_enter,
    .leave = p_leave,
    .motion = p_motion,
    .button = p_button,
    .axis = p_axis,
    .frame = p_frame,
    .axis_source = p_source,
    .axis_stop = p_stop,
    .axis_discrete = p_discrete,
};

static struct wl_pointer* pointer;

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    if (wl_boot() || !wl_seat_g) return 2;

    // a pointer of our own that counts leaves
    pointer = wl_seat_get_pointer(wl_seat_g);
    wl_pointer_add_listener(pointer, &listener, NULL);

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "toplevel-gone-pointer", 300, 200, 0xFF40A040u);
    printf("toplevel gone pointer: mapped\n");

    while (!wlp_focus && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("pointer entered\n");

    // the scenario stops moving the pointer first
    while (access("go-destroy", F_OK) != 0) {
        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }

    // the role goes, the surface stays; no input follows
    wl_display_roundtrip(wl_dpy);

    if (!wlp_focus) {
        fprintf(stderr, "the pointer left before the toplevel went\n");
        return 1;
    }

    leaves = 0;
    xdg_toplevel_destroy(top.tl);
    xdg_surface_destroy(top.xs);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (leaves != 1) {
        fprintf(stderr, "the pointer did not leave the role-less surface (%d leaves)\n", leaves);
        return 1;
    }

    printf("toplevel gone pointer done\n");

    return 0;
}
