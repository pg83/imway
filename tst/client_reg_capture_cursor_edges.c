#include "wl_util.h"

#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>
#include <single-pixel-buffer-v1-client-protocol.h>

// Cursor capture with nothing worth copying. Before any client set a cursor
// the session sizes itself 1x1 and its frame fails; an shm cursor has no CPU
// copy to deliver from, and a cursor surface without content sizes the
// session 1x1 — both fail their frame too. Last, a single-pixel cursor is
// copied into a buffer whose memfd was shrunk under the compositor: the
// copy faults and the client gets a wl_shm error. With the argument
// "unguarded" the memfd stays whole for a compositor that cannot set up
// its SIGBUS guard (IMWAY_CHAOS=sigbus-record=1): the same error, without
// touching the memory.

static struct ext_output_image_capture_source_manager_v1* source_mgr;
static struct ext_image_copy_capture_manager_v1* copy_mgr;
static struct wl_output* output;
static struct wp_single_pixel_buffer_manager_v1* sp_mgr;

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(iface, ext_output_image_capture_source_manager_v1_interface.name))
        source_mgr = wl_registry_bind(registry, name,
            &ext_output_image_capture_source_manager_v1_interface, 1);
    else if (!strcmp(iface, ext_image_copy_capture_manager_v1_interface.name))
        copy_mgr = wl_registry_bind(registry, name,
            &ext_image_copy_capture_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && !output)
        output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    else if (!strcmp(iface, wp_single_pixel_buffer_manager_v1_interface.name))
        sp_mgr = wl_registry_bind(registry, name,
            &wp_single_pixel_buffer_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void on_cursor_enter(void* d, struct ext_image_copy_capture_cursor_session_v1* c) {
    (void)d; (void)c;
}
static void on_cursor_leave(void* d, struct ext_image_copy_capture_cursor_session_v1* c) {
    (void)d; (void)c;
}
static void on_cursor_position(void* d, struct ext_image_copy_capture_cursor_session_v1* c,
                               int32_t x, int32_t y) {
    (void)d; (void)c; (void)x; (void)y;
}
static void on_cursor_hotspot(void* d, struct ext_image_copy_capture_cursor_session_v1* c,
                              int32_t x, int32_t y) {
    (void)d; (void)c; (void)x; (void)y;
}
static const struct ext_image_copy_capture_cursor_session_v1_listener cursor_listener = {
    .enter = on_cursor_enter,
    .leave = on_cursor_leave,
    .position = on_cursor_position,
    .hotspot = on_cursor_hotspot,
};

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
                             struct wl_array* dev) {
    (void)d; (void)s; (void)dev;
}
static void on_dmabuf_format(void* d, struct ext_image_copy_capture_session_v1* s,
                             uint32_t f, struct wl_array* mods) {
    (void)d; (void)s; (void)f; (void)mods;
}
static void on_done(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
    constraints_done = 1;
}
static void on_stopped(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
    fprintf(stderr, "the cursor session stopped\n");
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

static int last_fd = -1;

static struct wl_buffer* xrgb(int w, int h, uint32_t format) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("cursor-edges", 0);

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

// a fresh cursor session and its capture session, with the constraints in
static struct ext_image_copy_capture_session_v1* cursor_session(void) {
    struct ext_image_capture_source_v1* source =
        ext_output_image_capture_source_manager_v1_create_source(source_mgr, output);
    struct ext_image_copy_capture_cursor_session_v1* cursor =
        ext_image_copy_capture_manager_v1_create_pointer_cursor_session(copy_mgr, source, wl_ptr);

    ext_image_copy_capture_cursor_session_v1_add_listener(cursor, &cursor_listener, NULL);

    struct ext_image_copy_capture_session_v1* session =
        ext_image_copy_capture_cursor_session_v1_get_capture_session(cursor);

    constraints_done = 0;
    cap_w = cap_h = 0;
    ext_image_copy_capture_session_v1_add_listener(session, &session_listener, NULL);

    while (!constraints_done && wl_display_dispatch(wl_dpy) != -1) {
    }

    return session;
}

// with `shrink` the destination's memfd is cut to nothing before the
// capture is asked for: the compositor's mapping outlives the file size
static int capture(struct ext_image_copy_capture_session_v1* session, int shrink) {
    struct ext_image_copy_capture_frame_v1* frame = ext_image_copy_capture_session_v1_create_frame(session);
    struct wl_buffer* buffer = xrgb((int)cap_w, (int)cap_h, WL_SHM_FORMAT_XRGB8888);

    if (shrink && ftruncate(last_fd, 0) < 0) {
        exit(2);
    }

    frame_result = -1;
    ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, NULL);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
    ext_image_copy_capture_frame_v1_capture(frame);

    while (frame_result < 0 && wl_display_dispatch(wl_dpy) != -1) {
    }

    return frame_result;
}

static int expect_failed(const char* what, uint32_t w, uint32_t h, int got) {
    if (cap_w != w || cap_h != h || got != EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN) {
        fprintf(stderr, "%s: session %ux%u (want %ux%u), frame result %d\n", what, cap_w, cap_h, w, h, got);
        return 1;
    }

    printf("%s: ok\n", what);

    return 0;
}

static void set_cursor(struct wl_buffer* buffer, int w, int h) {
    struct wl_surface* surface = wl_compositor_create_surface(wl_comp);

    if (buffer) {
        wl_surface_attach(surface, buffer, 0, 0);
        wl_surface_damage(surface, 0, 0, w, h);
    }
    wl_surface_commit(surface);
    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, surface, 0, 0);
    wl_display_roundtrip(wl_dpy);
    wl_display_roundtrip(wl_dpy);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    int unguarded = argc > 1 && !strcmp(argv[1], "unguarded");

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!source_mgr || !copy_mgr || !output || !wl_ptr || !sp_mgr) {
        fprintf(stderr, "missing capture globals\n");
        return 2;
    }

    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "capture-cursor-edges", 300, 300, 0xff20c080u);
    wl_display_roundtrip(wl_dpy);

    // no client cursor anywhere yet
    if (expect_failed("no cursor", 1, 1, capture(cursor_session(), 0)))
        return 1;

    printf("client_reg_capture_cursor_edges: mapped\n");

    // the scenario brings the pointer over the window
    while (!wlp_enter_count && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("pointer entered\n");

    set_cursor(xrgb(2, 2, WL_SHM_FORMAT_ARGB8888), 2, 2);
    if (expect_failed("shm cursor", 2, 2, capture(cursor_session(), 0)))
        return 1;

    set_cursor(NULL, 0, 0);
    if (expect_failed("empty cursor", 1, 1, capture(cursor_session(), 0)))
        return 1;

    set_cursor(wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(sp_mgr, 0, 0, ~0u, ~0u), 1, 1);
    capture(cursor_session(), !unguarded);

    if (wl_expect_error("wl_buffer", WL_SHM_ERROR_INVALID_FD))
        return 1;

    printf("cursor capture edges done\n");

    return 0;
}
