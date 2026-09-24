#include "wl_util.h"

#include <wlr-screencopy-unstable-v1-client-protocol.h>

// #D-10: zwlr-screencopy v3 compat. capture_output must announce an shm
// buffer, accept the copy and deliver ready with the composed pixels.

static struct zwlr_screencopy_manager_v1* mgr;
static struct wl_output* output;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d;
    if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name))
        mgr = wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface,
                               version < 3 ? version : 3);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(registry, name, &wl_output_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static uint32_t buf_fmt, buf_w, buf_h, buf_stride;
static int buffer_announced, done_seen, ready_seen, failed_seen;

static void on_buffer(void* d, struct zwlr_screencopy_frame_v1* f,
                      uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride) {
    (void)d; (void)f;
    buf_fmt = fmt; buf_w = w; buf_h = h; buf_stride = stride;
    buffer_announced = 1;
}
static void on_flags(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t flags) {
    (void)d; (void)f;
    if (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) {
        fprintf(stderr, "unexpected y-invert\n");
        exit(1);
    }
}
static void on_ready(void* d, struct zwlr_screencopy_frame_v1* f,
                     uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)d; (void)f;
    if (!hi && !lo && !ns) {
        fprintf(stderr, "zero timestamp\n");
        exit(1);
    }
    ready_seen = 1;
}
static void on_failed(void* d, struct zwlr_screencopy_frame_v1* f) {
    (void)d; (void)f;
    failed_seen = 1;
}
static void on_damage(void* d, struct zwlr_screencopy_frame_v1* f,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)x; (void)y; (void)w; (void)h;
}
static void on_linux_dmabuf(void* d, struct zwlr_screencopy_frame_v1* f,
                            uint32_t fmt, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)fmt; (void)w; (void)h;
}
static void on_buffer_done(void* d, struct zwlr_screencopy_frame_v1* f) {
    (void)d; (void)f;
    done_seen = 1;
}
static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = on_buffer,
    .flags = on_flags,
    .ready = on_ready,
    .failed = on_failed,
    .damage = on_damage,
    .linux_dmabuf = on_linux_dmabuf,
    .buffer_done = on_buffer_done,
};

static int frame_seen;
static void on_frame(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d; (void)t;
    wl_callback_destroy(cb);
    frame_seen = 1;
}
static const struct wl_callback_listener frame_cb_listener = {on_frame};

int main(int argc, char** argv) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!mgr || !output) {
        fprintf(stderr, "no zwlr_screencopy_manager_v1\n");
        return 2;
    }

    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "screencopy-target", 300, 300, 0xffff00ff);
    wl_await_presented(ctx.surface);

    // a dump is compared with a later capture: let the window's first
    // frames settle on screen before copying
    for (int i = 0; argc > 1 && i < 10; i++) {
        struct wl_callback* cb = wl_surface_frame(ctx.surface);
        frame_seen = 0;
        wl_callback_add_listener(cb, &frame_cb_listener, NULL);
        wl_surface_commit(ctx.surface);
        while (!frame_seen && wl_display_dispatch(wl_dpy) != -1) {
        }
    }

    struct zwlr_screencopy_frame_v1* frame =
        zwlr_screencopy_manager_v1_capture_output(mgr, 0, output);
    zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, NULL);

    while ((!buffer_announced || !done_seen) && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (buf_fmt != WL_SHM_FORMAT_XRGB8888 || !buf_w || !buf_h) {
        fprintf(stderr, "bad buffer announce fmt=%u %ux%u\n", buf_fmt, buf_w, buf_h);
        return 1;
    }

    int size = (int)(buf_stride * buf_h);
    int fd = memfd_create("screencopy-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return 2;
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, (int)buf_w, (int)buf_h,
                                                         (int)buf_stride, buf_fmt);
    wl_shm_pool_destroy(pool);

    zwlr_screencopy_frame_v1_copy(frame, buffer);

    while (!ready_seen && !failed_seen && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (failed_seen) {
        fprintf(stderr, "screencopy failed\n");
        return 1;
    }

    // with a path: dump the copy as a PPM for the scenario to compare with
    // the compositor's own capture, and stay mapped until killed
    if (argc > 1) {
        FILE* out = fopen(argv[1], "wb");
        if (!out) return 2;
        fprintf(out, "P6\n%u %u\n255\n", buf_w, buf_h);
        for (uint32_t y = 0; y < buf_h; y++) {
            const uint32_t* row = (const uint32_t*)((const uint8_t*)px + (size_t)y * buf_stride);
            for (uint32_t x = 0; x < buf_w; x++) {
                unsigned char rgb[3] = {(unsigned char)(row[x] >> 16), (unsigned char)(row[x] >> 8), (unsigned char)row[x]};
                fwrite(rgb, 1, 3, out);
            }
        }
        fclose(out);
        printf("screencopy dumped\n");
        fflush(stdout);
        alarm(0);
        while (wl_display_dispatch(wl_dpy) != -1) {
        }
        return 0;
    }

    long magenta = 0;
    for (long i = 0; i < (long)buf_w * buf_h; i++) {
        if ((px[i] & 0xffffffu) == 0xff00ffu)
            magenta++;
    }
    if (magenta < 100) {
        fprintf(stderr, "no magenta in the copied frame (%ld px)\n", magenta);
        return 1;
    }

    printf("screencopy done\n");
    fflush(stdout);
    return 0;
}
