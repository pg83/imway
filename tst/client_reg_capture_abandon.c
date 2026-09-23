// Output copies abandoned while their readback is on the GPU (the scenario
// keeps the readback fence busy through IMWAY_CHAOS):
//   an ext-image-copy-capture frame destroyed in flight is dropped from the
//   readback, so the fence signalling later touches nothing of it, and the
//   session's next frame captures the green toplevel;
//   an ext frame, then a zwlr-screencopy frame, whose wl_buffer is destroyed
//   in flight fails once the readback lands, having nowhere to copy to.

#include "wl_util.h"

#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>
#include <wlr-screencopy-unstable-v1-client-protocol.h>

static struct ext_output_image_capture_source_manager_v1* source_mgr;
static struct ext_image_copy_capture_manager_v1* copy_mgr;
static struct zwlr_screencopy_manager_v1* wlr_mgr;
static struct wl_output* output;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, ext_output_image_capture_source_manager_v1_interface.name))
        source_mgr = wl_registry_bind(r, name, &ext_output_image_capture_source_manager_v1_interface, 1);
    else if (!strcmp(iface, ext_image_copy_capture_manager_v1_interface.name))
        copy_mgr = wl_registry_bind(r, name, &ext_image_copy_capture_manager_v1_interface, 1);
    else if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name))
        wlr_mgr = wl_registry_bind(r, name, &zwlr_screencopy_manager_v1_interface, 3);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(r, name, &wl_output_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static uint32_t cap_w, cap_h;
static int constraints_done;

static void on_buffer_size(void* d, struct ext_image_copy_capture_session_v1* s, uint32_t w, uint32_t h) {
    (void)d; (void)s;
    cap_w = w;
    cap_h = h;
}
static void on_shm_format(void* d, struct ext_image_copy_capture_session_v1* s, uint32_t f) {
    (void)d; (void)s; (void)f;
}
static void on_dmabuf_device(void* d, struct ext_image_copy_capture_session_v1* s, struct wl_array* dev) {
    (void)d; (void)s; (void)dev;
}
static void on_dmabuf_format(void* d, struct ext_image_copy_capture_session_v1* s, uint32_t f, struct wl_array* m) {
    (void)d; (void)s; (void)f; (void)m;
}
static void on_done(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
    constraints_done = 1;
}
static void on_stopped(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
    fprintf(stderr, "session stopped\n");
    exit(1);
}
static const struct ext_image_copy_capture_session_v1_listener session_listener = {
    .buffer_size = on_buffer_size,
    .shm_format = on_shm_format,
    .dmabuf_device = on_dmabuf_device,
    .dmabuf_format = on_dmabuf_format,
    .done = on_done,
    .stopped = on_stopped,
};

struct shot {
    struct ext_image_copy_capture_frame_v1* frame;
    int ready, failed;
};

static void on_transform(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t t) {
    (void)d; (void)f; (void)t;
}
static void on_damage(void* d, struct ext_image_copy_capture_frame_v1* f, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)f; (void)x; (void)y; (void)w; (void)h;
}
static void on_ptime(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)d; (void)f; (void)hi; (void)lo; (void)ns;
}
static void on_ready(void* d, struct ext_image_copy_capture_frame_v1* f) {
    (void)f;
    ((struct shot*)d)->ready = 1;
}
static void on_failed(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t r) {
    (void)f; (void)r;
    ((struct shot*)d)->failed = 1;
}
static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = on_transform,
    .damage = on_damage,
    .presentation_time = on_ptime,
    .ready = on_ready,
    .failed = on_failed,
};

static int frame_done;

static void on_frame_done(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d; (void)t;
    wl_callback_destroy(cb);
    frame_done = 1;
}
static const struct wl_callback_listener frame_done_listener = {on_frame_done};

// a new frame of the toplevel: a capture asked for before it goes onto the
// GPU in the pass that sends its frame callback
static void next_frame(struct wl_toplevel_ctx* top, uint32_t color) {
    struct wl_callback* cb = wl_surface_frame(top->surface);

    frame_done = 0;
    wl_callback_add_listener(cb, &frame_done_listener, NULL);
    wl_surface_attach(top->surface, wl_solid(top->w, top->h, color), 0, 0);
    wl_surface_damage(top->surface, 0, 0, top->w, top->h);
    wl_surface_commit(top->surface);
    while (!frame_done && wl_display_dispatch(wl_dpy) != -1) {
    }
}

static void wlr_buffer(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride) {
    (void)d; (void)f; (void)fmt; (void)w; (void)h; (void)stride;
}
static void wlr_flags(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t flags) {
    (void)d; (void)f; (void)flags;
}
static void wlr_ready(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)f; (void)hi; (void)lo; (void)ns;
    ((struct shot*)d)->ready = 1;
}
static void wlr_failed(void* d, struct zwlr_screencopy_frame_v1* f) {
    (void)f;
    ((struct shot*)d)->failed = 1;
}
static void wlr_damage(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)x; (void)y; (void)w; (void)h;
}
static void wlr_dmabuf(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t fmt, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)fmt; (void)w; (void)h;
}
static int wlr_announced;
static void wlr_buffer_done(void* d, struct zwlr_screencopy_frame_v1* f) {
    (void)d; (void)f;
    wlr_announced = 1;
}
static const struct zwlr_screencopy_frame_v1_listener wlr_listener = {
    .buffer = wlr_buffer,
    .flags = wlr_flags,
    .ready = wlr_ready,
    .failed = wlr_failed,
    .damage = wlr_damage,
    .linux_dmabuf = wlr_dmabuf,
    .buffer_done = wlr_buffer_done,
};

static struct wl_buffer* make_buffer(int stride, int size, uint32_t** px) {
    int fd = memfd_create("capture-abandon", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(2);
    }
    if (px) {
        *px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    }
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, (int)cap_w, (int)cap_h, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static void capture(struct shot* s, struct ext_image_copy_capture_session_v1* session, struct wl_buffer* buffer) {
    memset(s, 0, sizeof(*s));
    s->frame = ext_image_copy_capture_session_v1_create_frame(session);
    ext_image_copy_capture_frame_v1_add_listener(s->frame, &frame_listener, s);
    ext_image_copy_capture_frame_v1_attach_buffer(s->frame, buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(s->frame, 0, 0, (int)cap_w, (int)cap_h);
    ext_image_copy_capture_frame_v1_capture(s->frame);
}

// the busy readback holds the GPU copy for a while: a capture asked for
// meanwhile fails after its retries, so try until one lands, which also
// leaves the readback idle for the next copy
static int land(struct shot* s, struct ext_image_copy_capture_session_v1* session, struct wl_buffer* buffer, struct wl_toplevel_ctx* top) {
    for (int i = 0; i < 40; i++) {
        capture(s, session, buffer);
        next_frame(top, i & 1 ? 0xFF00FF00u : 0xFF00F000u);
        while (!s->ready && !s->failed && wl_display_dispatch(wl_dpy) != -1) {
        }
        ext_image_copy_capture_frame_v1_destroy(s->frame);
        if (s->ready) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 2;

    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!source_mgr || !copy_mgr || !wlr_mgr || !output) {
        fprintf(stderr, "missing capture globals\n");
        return 2;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "capture-abandon", 300, 300, 0xFF00FF00u);

    struct ext_image_capture_source_v1* source = ext_output_image_capture_source_manager_v1_create_source(source_mgr, output);
    struct ext_image_copy_capture_session_v1* session = ext_image_copy_capture_manager_v1_create_session(copy_mgr, source, 0);
    ext_image_copy_capture_session_v1_add_listener(session, &session_listener, NULL);
    while (!constraints_done && wl_display_dispatch(wl_dpy) != -1) {
    }

    int stride = (int)cap_w * 4, size = stride * (int)cap_h;
    uint32_t* px = NULL;
    struct wl_buffer* buffer = make_buffer(stride, size, &px);

    struct shot abandoned, kept;

    capture(&abandoned, session, buffer);
    next_frame(&top, 0xFF00C000u);
    if (abandoned.ready || abandoned.failed) {
        fprintf(stderr, "the capture finished with the readback still busy\n");
        return 1;
    }
    ext_image_copy_capture_frame_v1_destroy(abandoned.frame);
    wl_display_roundtrip(wl_dpy);
    printf("capture-abandon: abandoned in flight\n");

    memset(px, 0, (size_t)size);
    if (!land(&kept, session, buffer, &top)) {
        fprintf(stderr, "no capture landed after the abandoned one\n");
        return 1;
    }

    long green = 0;
    for (long i = 0; i < (long)cap_w * cap_h; i++) {
        uint32_t v = px[i] & 0xffffffu;
        if ((v & 0xff00ffu) == 0 && (v & 0xff00u) >= 0xc000u)
            green++;
    }
    if (green < 1000) {
        fprintf(stderr, "the capture after the abandoned one has no green window (%ld px)\n", green);
        return 1;
    }

    printf("capture-abandon: next capture ready\n");

    // the destination goes while the copy is on the GPU
    struct shot orphan;
    struct wl_buffer* gone = make_buffer(stride, size, NULL);

    capture(&orphan, session, gone);
    next_frame(&top, 0xFF00C000u);
    if (orphan.ready || orphan.failed) {
        fprintf(stderr, "the orphaned capture finished with the readback still busy\n");
        return 1;
    }
    wl_buffer_destroy(gone);
    while (!orphan.ready && !orphan.failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    ext_image_copy_capture_frame_v1_destroy(orphan.frame);
    if (!orphan.failed) {
        fprintf(stderr, "a capture whose buffer went in flight did not fail\n");
        return 1;
    }
    printf("capture-abandon: bufferless capture failed\n");

    if (!land(&kept, session, buffer, &top)) {
        fprintf(stderr, "no capture landed after the bufferless one\n");
        return 1;
    }

    // the same for a zwlr-screencopy copy
    struct shot wlr;

    memset(&wlr, 0, sizeof(wlr));
    wlr_announced = 0;
    struct zwlr_screencopy_frame_v1* copy = zwlr_screencopy_manager_v1_capture_output(wlr_mgr, 0, output);
    zwlr_screencopy_frame_v1_add_listener(copy, &wlr_listener, &wlr);
    while (!wlr_announced && wl_display_dispatch(wl_dpy) != -1) {
    }
    gone = make_buffer(stride, size, NULL);
    zwlr_screencopy_frame_v1_copy(copy, gone);
    next_frame(&top, 0xFF00C000u);
    if (wlr.ready || wlr.failed) {
        fprintf(stderr, "the orphaned copy finished with the readback still busy\n");
        return 1;
    }
    wl_buffer_destroy(gone);
    while (!wlr.ready && !wlr.failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    zwlr_screencopy_frame_v1_destroy(copy);
    if (!wlr.failed) {
        fprintf(stderr, "a screencopy whose buffer went in flight did not fail\n");
        return 1;
    }
    printf("capture-abandon: bufferless screencopy failed\n");

    wl_buffer_destroy(buffer);
    ext_image_copy_capture_session_v1_destroy(session);
    ext_image_capture_source_v1_destroy(source);
    return 0;
}
