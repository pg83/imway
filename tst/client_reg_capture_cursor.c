#include "wl_util.h"

#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>
#include <single-pixel-buffer-v1-client-protocol.h>

// ext-image-copy-capture's cursor half: a pointer cursor session follows the
// cursor over the captured output, announces the same buffer constraints as
// an ordinary session once one is asked for, and refuses a second one.

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

static int cursor_entered, cursor_left, positions;
static int32_t cursor_x, cursor_y;

static void on_cursor_enter(void* d, struct ext_image_copy_capture_cursor_session_v1* c) {
    (void)d; (void)c;
    cursor_entered = 1;
}
static void on_cursor_leave(void* d, struct ext_image_copy_capture_cursor_session_v1* c) {
    (void)d; (void)c;
    cursor_left = 1;
}
static void on_cursor_position(void* d, struct ext_image_copy_capture_cursor_session_v1* c,
                               int32_t x, int32_t y) {
    (void)d; (void)c;
    cursor_x = x;
    cursor_y = y;
    positions++;
}
static int32_t hot_x = -1, hot_y = -1;
static void on_cursor_hotspot(void* d, struct ext_image_copy_capture_cursor_session_v1* c,
                              int32_t x, int32_t y) {
    (void)d; (void)c;
    hot_x = x;
    hot_y = y;
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

static int frame_ready, frame_failed;

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
    frame_ready = 1;
}
static void on_failed(void* d, struct ext_image_copy_capture_frame_v1* f, uint32_t r) {
    (void)d; (void)f;
    fprintf(stderr, "the cursor capture failed: %u\n", r);
    frame_failed = 1;
}
static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = on_transform,
    .damage = on_damage,
    .presentation_time = on_ptime,
    .ready = on_ready,
    .failed = on_failed,
};

// A single-pixel cursor. The compositor keeps a CPU copy of a surface's
// content only for this buffer kind, and the cursor capture copies from
// that copy, so this is the cursor a session can actually deliver.
#define CURSOR_W 1
#define CURSOR_H 1
#define CURSOR_ARGB 0xff3070f0u

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!source_mgr || !copy_mgr || !output || !wl_ptr || !sp_mgr) {
        fprintf(stderr, "missing capture globals (src=%p copy=%p out=%p ptr=%p spb=%p)\n",
                (void*)source_mgr, (void*)copy_mgr, (void*)output, (void*)wl_ptr,
                (void*)sp_mgr);
        return 2;
    }

    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "capture-cursor", 300, 300, 0xff20c080u);
    wl_display_roundtrip(wl_dpy);
    printf("client_reg_capture_cursor: mapped\n");

    struct ext_image_capture_source_v1* source =
        ext_output_image_capture_source_manager_v1_create_source(source_mgr, output);
    struct ext_image_copy_capture_cursor_session_v1* cursor =
        ext_image_copy_capture_manager_v1_create_pointer_cursor_session(copy_mgr, source, wl_ptr);

    ext_image_copy_capture_cursor_session_v1_add_listener(cursor, &cursor_listener, NULL);

    while (!cursor_entered && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (!cursor_entered) {
        fprintf(stderr, "the cursor session never entered the output\n");
        return 1;
    }

    printf("cursor session entered\n");

    // A cursor of our own: the session announces its size and copies its
    // pixels, so without one there is nothing to capture but a 1x1 stub.
    // Setting one needs the pointer over our window, which is the scenario's
    // job, and it keeps aiming until this says it landed.
    while (!wlp_enter_count && wl_display_dispatch(wl_dpy) != -1) {
    }

    struct wl_surface* cursor_surface = wl_compositor_create_surface(wl_comp);

    struct wl_buffer* cursor_buffer = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
        sp_mgr, 0x30000000u, 0x70000000u, 0xf0000000u, 0xffffffffu);

    wl_surface_attach(cursor_surface, cursor_buffer, 0, 0);
    wl_surface_damage(cursor_surface, 0, 0, CURSOR_W, CURSOR_H);
    wl_surface_commit(cursor_surface);
    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor_surface, 4, 4);
    wl_display_roundtrip(wl_dpy);
    printf("cursor set\n");

    // the session reports where the cursor went, in output coordinates; the
    // pointer is over our window by now, so this is not the origin
    while (!(positions && cursor_x > 0) && wl_display_dispatch(wl_dpy) != -1) {
    }

    printf("cursor position %d %d\n", cursor_x, cursor_y);

    struct ext_image_copy_capture_session_v1* session =
        ext_image_copy_capture_cursor_session_v1_get_capture_session(cursor);

    ext_image_copy_capture_session_v1_add_listener(session, &session_listener, NULL);

    while (!constraints_done && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (cap_w != CURSOR_W || cap_h != CURSOR_H) {
        fprintf(stderr, "the cursor session sized itself %ux%u, not %ux%u\n",
                cap_w, cap_h, CURSOR_W, CURSOR_H);
        return 1;
    }

    printf("cursor session constraints %ux%u\n", cap_w, cap_h);

    int stride = (int)cap_w * 4, size = stride * (int)cap_h;
    int fd = memfd_create("cursor-capture", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) return 2;

    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* into = wl_shm_pool_create_buffer(pool, 0, (int)cap_w, (int)cap_h,
                                                       stride, WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);

    struct ext_image_copy_capture_frame_v1* frame =
        ext_image_copy_capture_session_v1_create_frame(session);

    ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, NULL);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, into);
    ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, (int)cap_w, (int)cap_h);
    ext_image_copy_capture_frame_v1_capture(frame);

    while (!frame_ready && !frame_failed && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (frame_failed) return 1;

    long ours = 0;

    for (long i = 0; i < (long)cap_w * cap_h; i++) {
        if ((px[i] & 0xffffffu) == (CURSOR_ARGB & 0xffffffu)) ours++;
    }

    if (ours < (long)cap_w * cap_h / 2) {
        fprintf(stderr, "the captured cursor is not ours (%ld of %ld px)\n",
                ours, (long)cap_w * cap_h);
        return 1;
    }

    printf("cursor captured %ld px\n", ours);

    // the pointer moving straight down, then the same cursor with its
    // hotspot moved down only: each change is reported although the other
    // coordinate stays put
    int32_t x0 = cursor_x, y0 = cursor_y;

    printf("cursor ready to move\n");
    while (!(cursor_x == x0 && cursor_y != y0) && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("cursor moved down to %d %d\n", cursor_x, cursor_y);

    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor_surface, 4, 6);
    while (!(hot_x == 4 && hot_y == 6) && wl_display_dispatch(wl_dpy) != -1) {
    }
    printf("cursor hotspot %d %d\n", hot_x, hot_y);

    // one session per cursor session, and the second one is a protocol error
    ext_image_copy_capture_cursor_session_v1_get_capture_session(cursor);

    return wl_expect_error("ext_image_copy_capture_cursor_session_v1",
                           EXT_IMAGE_COPY_CAPTURE_CURSOR_SESSION_V1_ERROR_DUPLICATE_SESSION);
}
