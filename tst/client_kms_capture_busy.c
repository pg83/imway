// zwlr-screencopy copies of the output while the GPU is slow to finish them
// (the scenario keeps the readback fence busy through IMWAY_CHAOS). Next to
// a green toplevel whose frame callbacks mark when a copy is on the GPU:
//   two copies ride one readback; the second is destroyed while it is in
//   flight ("one abandoned"), and the first then fails when the output
//   changes mode under it ("in-flight failed");
//   a copy made while the GPU still works on an older frame's readback
//   fails after its retries ("busy failed"), and the one already on the GPU
//   completes once the fence signals ("stalled ready").

#include "wl_util.h"

#include <wlr-screencopy-unstable-v1-client-protocol.h>

static struct zwlr_screencopy_manager_v1* mgr;
static struct wl_output* output;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name))
        mgr = wl_registry_bind(r, name, &zwlr_screencopy_manager_v1_interface, v < 3 ? v : 3);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(r, name, &wl_output_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

struct copy {
    struct zwlr_screencopy_frame_v1* frame;
    uint32_t w, h, stride;
    int announced, ready, failed;
};

static void on_buffer(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride) {
    struct copy* c = d;
    (void)f; (void)fmt;
    c->w = w;
    c->h = h;
    c->stride = stride;
}
static void on_flags(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t flags) {
    (void)d; (void)f; (void)flags;
}
static void on_ready(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)f; (void)hi; (void)lo; (void)ns;
    ((struct copy*)d)->ready = 1;
}
static void on_failed(void* d, struct zwlr_screencopy_frame_v1* f) {
    (void)f;
    ((struct copy*)d)->failed = 1;
}
static void on_damage(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)x; (void)y; (void)w; (void)h;
}
static void on_dmabuf(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t fmt, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)fmt; (void)w; (void)h;
}
static void on_buffer_done(void* d, struct zwlr_screencopy_frame_v1* f) {
    (void)f;
    ((struct copy*)d)->announced = 1;
}
static const struct zwlr_screencopy_frame_v1_listener copy_listener = {
    .buffer = on_buffer,
    .flags = on_flags,
    .ready = on_ready,
    .failed = on_failed,
    .damage = on_damage,
    .linux_dmabuf = on_dmabuf,
    .buffer_done = on_buffer_done,
};

static void announce(struct copy* c) {
    memset(c, 0, sizeof(*c));
    c->frame = zwlr_screencopy_manager_v1_capture_output(mgr, 0, output);
    zwlr_screencopy_frame_v1_add_listener(c->frame, &copy_listener, c);
    while (!c->announced && wl_display_dispatch(wl_dpy) != -1) {
    }
}

static struct wl_buffer* destination(struct copy* c) {
    int size = (int)(c->stride * c->h);
    int fd = memfd_create("capture-busy", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(2);
    }
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, (int)c->w, (int)c->h, (int)c->stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static int frame_done;

static void on_frame_done(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d; (void)t;
    wl_callback_destroy(cb);
    frame_done = 1;
}
static const struct wl_callback_listener frame_listener = {on_frame_done};

// a new frame of the toplevel: the copies requested before it are
// fulfilled in the same pass that sends its frame callback
static void next_frame(struct wl_toplevel_ctx* top, uint32_t color) {
    struct wl_callback* cb = wl_surface_frame(top->surface);

    frame_done = 0;
    wl_callback_add_listener(cb, &frame_listener, NULL);
    wl_surface_attach(top->surface, wl_solid(top->w, top->h, color), 0, 0);
    wl_surface_damage(top->surface, 0, 0, top->w, top->h);
    wl_surface_commit(top->surface);
    while (!frame_done && wl_display_dispatch(wl_dpy) != -1) {
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 2;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!mgr || !output) {
        fprintf(stderr, "no screencopy\n");
        return 2;
    }

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "capture-busy", 200, 150, 0xFF00FF00u);
    printf("client_kms_capture_busy: mapped\n");

    struct copy kept, dropped;
    struct wl_buffer* kept_buf;
    struct wl_buffer* dropped_buf;

    announce(&kept);
    announce(&dropped);
    kept_buf = destination(&kept);
    dropped_buf = destination(&dropped);
    zwlr_screencopy_frame_v1_copy(kept.frame, kept_buf);
    zwlr_screencopy_frame_v1_copy(dropped.frame, dropped_buf);
    next_frame(&top, 0xFF00C000u);
    if (kept.ready || kept.failed || dropped.ready || dropped.failed) {
        fprintf(stderr, "a copy finished with the readback still busy\n");
        return 1;
    }
    zwlr_screencopy_frame_v1_destroy(dropped.frame);
    wl_display_roundtrip(wl_dpy);
    printf("client_kms_capture_busy: one abandoned\n");

    while (!kept.ready && !kept.failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!kept.failed) {
        fprintf(stderr, "the copy in flight across the mode change did not fail\n");
        return 1;
    }
    zwlr_screencopy_frame_v1_destroy(kept.frame);
    printf("client_kms_capture_busy: in-flight failed\n");

    struct copy stalled, busy;
    struct wl_buffer* stalled_buf;
    struct wl_buffer* busy_buf;

    // the rebuilt output has no frame to copy until one is composed
    next_frame(&top, 0xFF00C000u);
    announce(&stalled);
    stalled_buf = destination(&stalled);
    zwlr_screencopy_frame_v1_copy(stalled.frame, stalled_buf);
    next_frame(&top, 0xFF00FF00u);

    announce(&busy);
    busy_buf = destination(&busy);
    zwlr_screencopy_frame_v1_copy(busy.frame, busy_buf);
    while (!busy.ready && !busy.failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!busy.failed || stalled.ready || stalled.failed) {
        fprintf(stderr, "busy copy: ready=%d failed=%d, stalled copy ready=%d failed=%d\n",
                busy.ready, busy.failed, stalled.ready, stalled.failed);
        return 1;
    }
    printf("client_kms_capture_busy: busy failed\n");

    while (!stalled.ready && !stalled.failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!stalled.ready) {
        fprintf(stderr, "the stalled copy did not complete\n");
        return 1;
    }
    printf("client_kms_capture_busy: stalled ready\n");

    zwlr_screencopy_frame_v1_destroy(busy.frame);
    zwlr_screencopy_frame_v1_destroy(stalled.frame);
    wl_buffer_destroy(kept_buf);
    wl_buffer_destroy(dropped_buf);
    wl_buffer_destroy(stalled_buf);
    wl_buffer_destroy(busy_buf);
    wl_display_roundtrip(wl_dpy);
    return 0;
}
