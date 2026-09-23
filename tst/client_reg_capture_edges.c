#include "wl_util.h"

#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>
#include <single-pixel-buffer-v1-client-protocol.h>
#include <wlr-screencopy-unstable-v1-client-protocol.h>

// The output capture paths that do not end in a ready frame. ext-image-copy-
// capture must bounce every buffer that misses the announced constraints
// (width, height, format, stride, not shm at all) with buffer_constraints,
// fail a frame whose buffer died before the capture ran, and keep working
// after all of it. zwlr-screencopy must clamp a region off the output to a
// 1x1 frame, report damage for copy_with_damage, and fail a copy whose
// buffer died. The *-sigbus modes shrink the destination's memfd under the
// compositor's mapping: the copy into it faults, and the client is told its
// buffer could not be accessed instead of the compositor dying. The
// *-unguarded modes leave the memfd whole for a compositor that cannot set
// up its SIGBUS guard (IMWAY_CHAOS=sigbus-record=N): it must not touch the
// memory unguarded, and tells the client the same.

static struct ext_output_image_capture_source_manager_v1* source_mgr;
static struct ext_image_copy_capture_manager_v1* copy_mgr;
static struct zwlr_screencopy_manager_v1* wlr_mgr;
static struct wp_single_pixel_buffer_manager_v1* spb_mgr;
static struct wl_output* output;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d;
    if (!strcmp(iface, ext_output_image_capture_source_manager_v1_interface.name))
        source_mgr = wl_registry_bind(registry, name,
            &ext_output_image_capture_source_manager_v1_interface, 1);
    else if (!strcmp(iface, ext_image_copy_capture_manager_v1_interface.name))
        copy_mgr = wl_registry_bind(registry, name,
            &ext_image_copy_capture_manager_v1_interface, 1);
    else if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name))
        wlr_mgr = wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface,
                                   version < 3 ? version : 3);
    else if (!strcmp(iface, wp_single_pixel_buffer_manager_v1_interface.name))
        spb_mgr = wl_registry_bind(registry, name,
            &wp_single_pixel_buffer_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(registry, name, &wl_output_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static int last_fd = -1;

static struct wl_buffer* make_buffer(int w, int h, int stride, uint32_t format) {
    int size = stride * h;
    int fd = memfd_create("capture-edges", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(2);
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, format);

    wl_shm_pool_destroy(pool);

    if (last_fd >= 0)
        close(last_fd);
    last_fd = fd;

    return buffer;
}

// ---- ext-image-copy-capture ----
static uint32_t cap_w, cap_h;
static int constraints_done;

static void on_buffer_size(void* d, struct ext_image_copy_capture_session_v1* s,
                           uint32_t w, uint32_t h) {
    (void)d; (void)s;
    cap_w = w;
    cap_h = h;
}
static void on_shm_format(void* d, struct ext_image_copy_capture_session_v1* s, uint32_t f) {
    (void)d; (void)s; (void)f;
}
static void on_dmabuf_device(void* d, struct ext_image_copy_capture_session_v1* s,
                             struct wl_array* a) {
    (void)d; (void)s; (void)a;
}
static void on_dmabuf_format(void* d, struct ext_image_copy_capture_session_v1* s,
                             uint32_t f, struct wl_array* m) {
    (void)d; (void)s; (void)f; (void)m;
}
static void on_done(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
    constraints_done = 1;
}
static void on_stopped(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
    fprintf(stderr, "the output session stopped\n");
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

// -1 while pending, 0x100 for ready, else the failure reason
static int frame_result;

static void on_transform(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t t) {
    (void)d; (void)f; (void)t;
}
static void on_damage(void* d, struct ext_image_copy_capture_frame_v1* f,
                      int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)f; (void)x; (void)y; (void)w; (void)h;
}
static void on_ptime(void* d, struct ext_image_copy_capture_frame_v1* f,
                     uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)d; (void)f; (void)hi; (void)lo; (void)ns;
}
static void on_ready(void* d, struct ext_image_copy_capture_frame_v1* f) {
    (void)d; (void)f;
    frame_result = 0x100;
}
static void on_failed(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t r) {
    (void)d; (void)f;
    frame_result = (int)r;
}
static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = on_transform,
    .damage = on_damage,
    .presentation_time = on_ptime,
    .ready = on_ready,
    .failed = on_failed,
};

// one frame into `buffer`; `first`, when given, is attached ahead of it and
// replaced. With `kill` the buffer is destroyed in the same flush as the
// capture request, before the compositor gets to fulfil it.
static int capture(struct ext_image_copy_capture_session_v1* session,
                   struct wl_buffer* first, struct wl_buffer* buffer, int kill) {
    struct ext_image_copy_capture_frame_v1* frame =
        ext_image_copy_capture_session_v1_create_frame(session);

    frame_result = -1;
    ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, NULL);
    if (first)
        ext_image_copy_capture_frame_v1_attach_buffer(frame, first);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
    ext_image_copy_capture_frame_v1_capture(frame);
    if (kill)
        wl_buffer_destroy(buffer);

    while (frame_result < 0 && wl_display_dispatch(wl_dpy) != -1) {
    }

    ext_image_copy_capture_frame_v1_destroy(frame);

    return frame_result;
}

static struct ext_image_copy_capture_session_v1* output_session(void) {
    struct ext_image_capture_source_v1* source =
        ext_output_image_capture_source_manager_v1_create_source(source_mgr, output);
    struct ext_image_copy_capture_session_v1* session =
        ext_image_copy_capture_manager_v1_create_session(copy_mgr, source, 0);

    ext_image_copy_capture_session_v1_add_listener(session, &session_listener, NULL);

    while (!constraints_done && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (!cap_w || !cap_h) {
        fprintf(stderr, "no constraints\n");
        exit(1);
    }

    return session;
}

static int expect(const char* what, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s: frame result %d, want %d\n", what, got, want);
        return 1;
    }

    printf("%s: ok\n", what);

    return 0;
}

static int run_output(void) {
    const int constraints = EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS;
    const int unknown = EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN;
    struct ext_image_copy_capture_session_v1* session = output_session();
    int w = (int)cap_w, h = (int)cap_h;

    // a session nobody ever asked a frame of goes away quietly
    ext_image_copy_capture_session_v1_destroy(output_session());

    if (expect("narrow", capture(session, NULL, make_buffer(w - 1, h, w * 4, WL_SHM_FORMAT_XRGB8888), 0), constraints) ||
        expect("short", capture(session, NULL, make_buffer(w, h - 1, w * 4, WL_SHM_FORMAT_XRGB8888), 0), constraints) ||
        expect("argb", capture(session, NULL, make_buffer(w, h, w * 4, WL_SHM_FORMAT_ARGB8888), 0), constraints) ||
        expect("stride", capture(session, NULL, make_buffer(w, h, w * 4 - 4, WL_SHM_FORMAT_XRGB8888), 0), constraints) ||
        expect("not-shm", capture(session, NULL,
                                  wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(spb_mgr, 0, 0, 0, ~0u), 0),
               constraints) ||
        expect("buffer-gone", capture(session, NULL, make_buffer(w, h, w * 4, WL_SHM_FORMAT_XRGB8888), 1), unknown)) {
        return 1;
    }

    // the replaced first attachment is let go; the frame lands in the second
    struct wl_buffer* first = make_buffer(w, h, w * 4, WL_SHM_FORMAT_XRGB8888);

    if (expect("reattached", capture(session, first, make_buffer(w, h, w * 4, WL_SHM_FORMAT_XRGB8888), 0), 0x100))
        return 1;

    printf("output edges done\n");

    return 0;
}

// under IMWAY_CHAOS=capture-submit=3 readback-fence=0: the first frame's
// copy is refused on all three frames it is retried on, the second one's
// readback is lost with the device, and the third lands
static int run_output_faults(void) {
    const int unknown = EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN;
    struct ext_image_copy_capture_session_v1* session = output_session();
    int w = (int)cap_w, h = (int)cap_h;

    if (expect("refused", capture(session, NULL, make_buffer(w, h, w * 4, WL_SHM_FORMAT_XRGB8888), 0), unknown) ||
        expect("lost", capture(session, NULL, make_buffer(w, h, w * 4, WL_SHM_FORMAT_XRGB8888), 0), unknown) ||
        expect("landed", capture(session, NULL, make_buffer(w, h, w * 4, WL_SHM_FORMAT_XRGB8888), 0), 0x100)) {
        return 1;
    }

    printf("output faults done\n");

    return 0;
}

// the destination's memfd is cut to nothing before the capture is asked
// for: the compositor's mapping outlives the file size, and the copy into
// it faults
static int run_output_sigbus(int cut) {
    struct ext_image_copy_capture_session_v1* session = output_session();
    struct ext_image_copy_capture_frame_v1* frame =
        ext_image_copy_capture_session_v1_create_frame(session);
    struct wl_buffer* buffer = make_buffer((int)cap_w, (int)cap_h, (int)cap_w * 4, WL_SHM_FORMAT_XRGB8888);

    if (cut && ftruncate(last_fd, 0) < 0)
        return 2;

    frame_result = -1;
    ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, NULL);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
    ext_image_copy_capture_frame_v1_capture(frame);

    while (frame_result < 0 && wl_display_dispatch(wl_dpy) != -1) {
    }

    return wl_expect_error("wl_buffer", WL_SHM_ERROR_INVALID_FD);
}

// ---- zwlr-screencopy ----
static uint32_t wlr_w, wlr_h, wlr_stride;
static int wlr_announced, wlr_ready, wlr_failed, wlr_damaged;

static void wlr_on_buffer(void* d, struct zwlr_screencopy_frame_v1* f,
                          uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride) {
    (void)d; (void)f; (void)fmt;
    wlr_w = w;
    wlr_h = h;
    wlr_stride = stride;
}
static void wlr_on_flags(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t flags) {
    (void)d; (void)f; (void)flags;
}
static void wlr_on_ready(void* d, struct zwlr_screencopy_frame_v1* f,
                         uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)d; (void)f; (void)hi; (void)lo; (void)ns;
    wlr_ready = 1;
}
static void wlr_on_failed(void* d, struct zwlr_screencopy_frame_v1* f) {
    (void)d; (void)f;
    wlr_failed = 1;
}
static void wlr_on_damage(void* d, struct zwlr_screencopy_frame_v1* f,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)x; (void)y;
    if (w == wlr_w && h == wlr_h)
        wlr_damaged = 1;
}
static void wlr_on_dmabuf(void* d, struct zwlr_screencopy_frame_v1* f,
                          uint32_t fmt, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)fmt; (void)w; (void)h;
}
static void wlr_on_buffer_done(void* d, struct zwlr_screencopy_frame_v1* f) {
    (void)d; (void)f;
    wlr_announced = 1;
}
static const struct zwlr_screencopy_frame_v1_listener wlr_listener = {
    .buffer = wlr_on_buffer,
    .flags = wlr_on_flags,
    .ready = wlr_on_ready,
    .failed = wlr_on_failed,
    .damage = wlr_on_damage,
    .linux_dmabuf = wlr_on_dmabuf,
    .buffer_done = wlr_on_buffer_done,
};

static struct zwlr_screencopy_frame_v1* wlr_frame(struct zwlr_screencopy_frame_v1* frame) {
    wlr_announced = wlr_ready = wlr_failed = wlr_damaged = 0;
    zwlr_screencopy_frame_v1_add_listener(frame, &wlr_listener, NULL);

    while (!wlr_announced && wl_display_dispatch(wl_dpy) != -1) {
    }

    return frame;
}

static void wlr_wait(void) {
    while (!wlr_ready && !wlr_failed && wl_display_dispatch(wl_dpy) != -1) {
    }
}

static int run_wlr(void) {
    // a region entirely off the output still makes a frame, one pixel big
    struct zwlr_screencopy_frame_v1* frame = wlr_frame(
        zwlr_screencopy_manager_v1_capture_output_region(wlr_mgr, 0, output, 100000, 100000, 50, 50));

    if (wlr_w != 1 || wlr_h != 1) {
        fprintf(stderr, "an off-output region announced %ux%u, not 1x1\n", wlr_w, wlr_h);
        return 1;
    }

    zwlr_screencopy_frame_v1_copy_with_damage(frame, make_buffer(1, 1, 4, WL_SHM_FORMAT_XRGB8888));
    wlr_wait();

    if (!wlr_ready || !wlr_damaged) {
        fprintf(stderr, "copy_with_damage: ready=%d damage=%d\n", wlr_ready, wlr_damaged);
        return 1;
    }

    zwlr_screencopy_frame_v1_destroy(frame);
    printf("wlr region: ok\n");

    // the buffer dies in the same flush as the copy request
    frame = wlr_frame(zwlr_screencopy_manager_v1_capture_output(wlr_mgr, 0, output));

    struct wl_buffer* buffer = make_buffer((int)wlr_w, (int)wlr_h, (int)wlr_stride, WL_SHM_FORMAT_XRGB8888);

    zwlr_screencopy_frame_v1_copy(frame, buffer);
    wl_buffer_destroy(buffer);
    wlr_wait();

    if (!wlr_failed) {
        fprintf(stderr, "a copy into a destroyed buffer did not fail\n");
        return 1;
    }

    zwlr_screencopy_frame_v1_destroy(frame);
    printf("wlr edges done\n");

    return 0;
}

static int run_wlr_sigbus(int cut) {
    struct zwlr_screencopy_frame_v1* frame =
        wlr_frame(zwlr_screencopy_manager_v1_capture_output(wlr_mgr, 0, output));
    struct wl_buffer* buffer = make_buffer((int)wlr_w, (int)wlr_h, (int)wlr_stride, WL_SHM_FORMAT_XRGB8888);

    if (cut && ftruncate(last_fd, 0) < 0)
        return 2;

    zwlr_screencopy_frame_v1_copy(frame, buffer);
    wlr_wait();

    return wl_expect_error("wl_buffer", WL_SHM_ERROR_INVALID_FD);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (argc < 2 || wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!source_mgr || !copy_mgr || !wlr_mgr || !spb_mgr || !output) {
        fprintf(stderr, "missing capture globals\n");
        return 2;
    }

    if (!strcmp(argv[1], "output")) return run_output();
    if (!strcmp(argv[1], "output-sigbus")) return run_output_sigbus(1);
    if (!strcmp(argv[1], "output-unguarded")) return run_output_sigbus(0);
    if (!strcmp(argv[1], "output-faults")) return run_output_faults();
    if (!strcmp(argv[1], "wlr")) return run_wlr();
    if (!strcmp(argv[1], "wlr-sigbus")) return run_wlr_sigbus(1);
    if (!strcmp(argv[1], "wlr-unguarded")) return run_wlr_sigbus(0);

    return 2;
}
