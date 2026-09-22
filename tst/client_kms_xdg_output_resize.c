// xdg-output across versions and a mode change. xdg_output objects from a
// v1, a v2 and a v3 manager, on a wl_output bound before and after
// wl_output.done existed: v1 gets no name, v1/v2 end their burst with
// xdg_output.done, v3 leaves that to wl_output.done where the wl_output has
// it. When the scenario switches the mode, every one of them learns the new
// logical size the same way.

#include "wl_util.h"
#include <xdg-output-unstable-v1-client-protocol.h>

static uint32_t output_name, manager_name;
static struct wl_output* out_v1;
static struct wl_output* out_v2;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wl_output_interface.name) && !output_name)
        output_name = name;
    else if (!strcmp(iface, zxdg_output_manager_v1_interface.name))
        manager_name = name;
    (void)r;
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

struct xout {
    const char* tag;
    int w, h, dones, named;
};

static void x_position(void* d, struct zxdg_output_v1* o, int32_t x, int32_t y) { (void)d; (void)o; (void)x; (void)y; }
static void x_size(void* d, struct zxdg_output_v1* o, int32_t w, int32_t h) {
    (void)o;
    struct xout* x = d;
    x->w = w;
    x->h = h;
}
static void x_done(void* d, struct zxdg_output_v1* o) {
    (void)o;
    ((struct xout*)d)->dones++;
}
static void x_name(void* d, struct zxdg_output_v1* o, const char* n) {
    (void)o; (void)n;
    ((struct xout*)d)->named = 1;
}
static void x_description(void* d, struct zxdg_output_v1* o, const char* s) { (void)d; (void)o; (void)s; }
static const struct zxdg_output_v1_listener xout_listener = {x_position, x_size, x_done, x_name, x_description};

static int out_dones;

static void o_geometry(void* d, struct wl_output* o, int32_t x, int32_t y, int32_t pw, int32_t ph,
                       int32_t sp, const char* make, const char* model, int32_t t) {
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)make; (void)model; (void)t;
}
static void o_mode(void* d, struct wl_output* o, uint32_t f, int32_t w, int32_t h, int32_t r) {
    (void)d; (void)o; (void)f; (void)w; (void)h; (void)r;
}
static void o_done(void* d, struct wl_output* o) {
    (void)d; (void)o;
    out_dones++;
}
static void o_scale(void* d, struct wl_output* o, int32_t s) { (void)d; (void)o; (void)s; }
static const struct wl_output_listener output_listener = {o_geometry, o_mode, o_done, o_scale, NULL, NULL};

static struct xout x1 = {"v1", 0, 0, 0, 0}, x2 = {"v2", 0, 0, 0, 0}, x3 = {"v3", 0, 0, 0, 0}, x3old = {"v3-on-v1", 0, 0, 0, 0};

static void get(struct wl_registry* reg, uint32_t version, struct wl_output* out, struct xout* into) {
    struct zxdg_output_manager_v1* m = wl_registry_bind(reg, manager_name, &zxdg_output_manager_v1_interface, version);

    zxdg_output_v1_add_listener(zxdg_output_manager_v1_get_xdg_output(m, out), &xout_listener, into);
}

static int sized(const struct xout* x, int w, int h) {
    return x->w == w && x->h == h;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(40);

    if (wl_boot()) return 2;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!output_name || !manager_name) return 2;

    out_v1 = wl_registry_bind(reg, output_name, &wl_output_interface, 1);
    out_v2 = wl_registry_bind(reg, output_name, &wl_output_interface, 2);
    wl_output_add_listener(out_v2, &output_listener, NULL);

    get(reg, 1, out_v2, &x1);
    get(reg, 2, out_v2, &x2);
    get(reg, 3, out_v2, &x3);
    get(reg, 3, out_v1, &x3old);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    if (x1.named || !x2.named || !x3.named) {
        fprintf(stderr, "names: v1=%d v2=%d v3=%d\n", x1.named, x2.named, x3.named);
        return 1;
    }

    if (x1.dones != 1 || x2.dones != 1 || x3.dones || x3old.dones) {
        fprintf(stderr, "xdg dones: v1=%d v2=%d v3=%d v3-on-v1=%d\n", x1.dones, x2.dones, x3.dones, x3old.dones);
        return 1;
    }

    printf("xdg outputs %dx%d\n", x3.w, x3.h);

    int dones = out_dones;

    for (int i = 0; i < 1000; i++) {
        if (sized(&x1, 1920, 1080) && sized(&x2, 1920, 1080) && sized(&x3, 1920, 1080) && sized(&x3old, 1920, 1080) &&
            x1.dones == 2 && x2.dones == 2 && out_dones > dones) {
            break;
        }

        if (wl_display_roundtrip(wl_dpy) < 0) return 1;
        usleep(20000);
    }

    printf("after the mode change: v1 %dx%d/%d v2 %dx%d/%d v3 %dx%d/%d v3-on-v1 %dx%d/%d wl_output dones %d\n",
           x1.w, x1.h, x1.dones, x2.w, x2.h, x2.dones, x3.w, x3.h, x3.dones, x3old.w, x3old.h, x3old.dones,
           out_dones - dones);

    if (!sized(&x1, 1920, 1080) || !sized(&x2, 1920, 1080) || !sized(&x3, 1920, 1080) || !sized(&x3old, 1920, 1080) ||
        x1.dones != 2 || x2.dones != 2 || x3.dones || x3old.dones || out_dones <= dones) {
        return 1;
    }

    printf("xdg output resize done\n");

    return 0;
}
