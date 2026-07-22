#include "wl_util.h"

#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>

// #D-9: ext-image-copy-capture. A magenta toplevel is on screen; an output
// capture session must deliver the announced constraints, accept an shm
// buffer and return a ready frame whose pixels contain the toplevel's color.

static struct ext_output_image_capture_source_manager_v1* source_mgr;
static struct ext_image_copy_capture_manager_v1* copy_mgr;
static struct wl_output* output;

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
}
static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static uint32_t cap_w, cap_h;
static int have_xrgb, constraints_done;

static void on_buffer_size(void* d, struct ext_image_copy_capture_session_v1* s,
                           uint32_t w, uint32_t h) {
    (void)d; (void)s;
    cap_w = w;
    cap_h = h;
}
static void on_shm_format(void* d, struct ext_image_copy_capture_session_v1* s,
                          uint32_t format) {
    (void)d; (void)s;
    if (format == WL_SHM_FORMAT_XRGB8888)
        have_xrgb = 1;
}
static void on_dmabuf_device(void* d, struct ext_image_copy_capture_session_v1* s,
                             struct wl_array* dev) {
    (void)d; (void)s; (void)dev;
}
static void on_dmabuf_format(void* d, struct ext_image_copy_capture_session_v1* s,
                             uint32_t format, struct wl_array* mods) {
    (void)d; (void)s; (void)format; (void)mods;
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

static int frame_ready, frame_failed, have_time;

static void on_transform(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t t) {
    (void)d; (void)f; (void)t;
}
static void on_damage(void* d, struct ext_image_copy_capture_frame_v1* f,
                      int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)d; (void)f; (void)x; (void)y; (void)w; (void)h;
}
static void on_ptime(void* d, struct ext_image_copy_capture_frame_v1* f,
                     uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)d; (void)f;
    if (hi || lo || ns)
        have_time = 1;
}
static void on_ready(void* d, struct ext_image_copy_capture_frame_v1* f) {
    (void)d; (void)f;
    frame_ready = 1;
}
static void on_failed(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t r) {
    (void)d; (void)f;
    fprintf(stderr, "capture failed: %u\n", r);
    frame_failed = 1;
}
static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = on_transform,
    .damage = on_damage,
    .presentation_time = on_ptime,
    .ready = on_ready,
    .failed = on_failed,
};

int main(void) {
    alarm(10);
    if (wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!source_mgr || !copy_mgr || !output) {
        fprintf(stderr, "missing capture globals (src=%p copy=%p out=%p)\n",
                (void*)source_mgr, (void*)copy_mgr, (void*)output);
        return 2;
    }

    struct wl_toplevel_ctx ctx;
    wl_make_toplevel(&ctx, "capture-target", 300, 300, 0xffff00ff);
    // a couple of presented frames so the magenta window is composited
    wl_display_roundtrip(wl_dpy);

    struct ext_image_capture_source_v1* source =
        ext_output_image_capture_source_manager_v1_create_source(source_mgr, output);
    struct ext_image_copy_capture_session_v1* session =
        ext_image_copy_capture_manager_v1_create_session(copy_mgr, source, 0);
    ext_image_copy_capture_session_v1_add_listener(session, &session_listener, NULL);

    while (!constraints_done && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (!cap_w || !cap_h || !have_xrgb) {
        fprintf(stderr, "bad constraints: %ux%u xrgb=%d\n", cap_w, cap_h, have_xrgb);
        return 1;
    }

    int stride = (int)cap_w * 4, size = stride * (int)cap_h;
    int fd = memfd_create("capture-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return 2;
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, (int)cap_w, (int)cap_h,
                                                         stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    struct ext_image_copy_capture_frame_v1* frame =
        ext_image_copy_capture_session_v1_create_frame(session);
    ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, NULL);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, (int)cap_w, (int)cap_h);
    ext_image_copy_capture_frame_v1_capture(frame);

    while (!frame_ready && !frame_failed && wl_display_dispatch(wl_dpy) != -1) {
    }
    if (frame_failed) return 1;
    if (!have_time) {
        fprintf(stderr, "no presentation_time before ready\n");
        return 1;
    }

    long magenta = 0;
    for (long i = 0; i < (long)cap_w * cap_h; i++) {
        uint32_t v = px[i] & 0xffffffu;
        if (v == 0xff00ffu)
            magenta++;
    }
    if (magenta < 100) {
        fprintf(stderr, "captured frame has no magenta window (%ld px)\n", magenta);
        return 1;
    }

    printf("capture done\n");
    fflush(stdout);
    return 0;
}
