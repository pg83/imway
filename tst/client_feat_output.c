// Feature: wl_output + xdg-output. The output must advertise a current mode,
// scale, a name, and an xdg logical size that matches the mode. Asserts the
// events are internally consistent (mode == logical size, scale 1, done fired).

#include "wl_util.h"
#include <xdg-output-unstable-v1-client-protocol.h>

static struct zxdg_output_manager_v1* xdg_out_mgr;
static struct wl_output* output;

static int32_t mode_w, mode_h, out_scale, log_w, log_h;
static int got_mode, got_scale, got_name, got_done, got_log_size, got_log_done;

static void out_geometry(void* d, struct wl_output* o, int32_t x, int32_t y, int32_t pw, int32_t ph,
                         int32_t sp, const char* make, const char* model, int32_t tr) {
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)make; (void)model; (void)tr;
}
static void out_mode(void* d, struct wl_output* o, uint32_t flags, int32_t w, int32_t h, int32_t r) {
    (void)d; (void)o; (void)r;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        mode_w = w; mode_h = h; got_mode = 1;
    }
}
static void out_done(void* d, struct wl_output* o) { (void)d; (void)o; got_done = 1; }
static void out_scale_ev(void* d, struct wl_output* o, int32_t s) {
    (void)d; (void)o; out_scale = s; got_scale = 1;
}
static void out_name(void* d, struct wl_output* o, const char* n) {
    (void)d; (void)o; got_name = (n && n[0]);
}
static void out_desc(void* d, struct wl_output* o, const char* n) { (void)d; (void)o; (void)n; }
static const struct wl_output_listener out_listener = {
    out_geometry, out_mode, out_done, out_scale_ev, out_name, out_desc,
};

static void xo_logical_position(void* d, struct zxdg_output_v1* o, int32_t x, int32_t y) {
    (void)d; (void)o; (void)x; (void)y;
}
static void xo_logical_size(void* d, struct zxdg_output_v1* o, int32_t w, int32_t h) {
    (void)d; (void)o; log_w = w; log_h = h; got_log_size = 1;
}
static void xo_done(void* d, struct zxdg_output_v1* o) { (void)d; (void)o; got_log_done = 1; }
static void xo_name(void* d, struct zxdg_output_v1* o, const char* n) { (void)d; (void)o; (void)n; }
static void xo_desc(void* d, struct zxdg_output_v1* o, const char* n) { (void)d; (void)o; (void)n; }
static const struct zxdg_output_v1_listener xo_listener = {
    xo_logical_position, xo_logical_size, xo_done, xo_name, xo_desc,
};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, wl_output_interface.name))
        output = wl_registry_bind(r, name, &wl_output_interface, 4);
    else if (!strcmp(iface, zxdg_output_manager_v1_interface.name))
        xdg_out_mgr = wl_registry_bind(r, name, &zxdg_output_manager_v1_interface, 3);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!output || !xdg_out_mgr) {
        fprintf(stderr, "client_feat_output: no output / xdg-output manager\n");
        return 1;
    }

    wl_output_add_listener(output, &out_listener, NULL);
    struct zxdg_output_v1* xo = zxdg_output_manager_v1_get_xdg_output(xdg_out_mgr, output);
    zxdg_output_v1_add_listener(xo, &xo_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);

    printf("client_feat_output: mode=%dx%d scale=%d logical=%dx%d name=%d done=%d/%d\n",
           mode_w, mode_h, out_scale, log_w, log_h, got_name, got_done, got_log_done);

    if (!got_mode || mode_w <= 0 || mode_h <= 0) { fprintf(stderr, "no current mode\n"); return 1; }
    if (!got_scale || out_scale != 1) { fprintf(stderr, "bad scale\n"); return 1; }
    if (!got_name) { fprintf(stderr, "no output name\n"); return 1; }
    if (!got_done) { fprintf(stderr, "no wl_output.done\n"); return 1; }
    if (!got_log_size || log_w != mode_w || log_h != mode_h) {
        fprintf(stderr, "xdg logical size %dx%d != mode %dx%d\n", log_w, log_h, mode_w, mode_h);
        return 1;
    }
    // xdg_output.done is deprecated since v3 (wl_output.done is authoritative)
    (void)got_log_done;

    printf("client_feat_output: ok\n");
    return 0;
}
