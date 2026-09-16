#include "wl_util.h"

#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>

// ext-image-copy-capture's cursor half: a pointer cursor session follows the
// cursor over the captured output, announces the same buffer constraints as
// an ordinary session once one is asked for, and refuses a second one.

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

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!source_mgr || !copy_mgr || !output || !wl_ptr) {
        fprintf(stderr, "missing capture globals (src=%p copy=%p out=%p ptr=%p)\n",
                (void*)source_mgr, (void*)copy_mgr, (void*)output, (void*)wl_ptr);
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

    // the scenario moves the pointer from here; the session reports where it
    // went, in output coordinates
    while (positions < 1 && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (positions < 1) {
        fprintf(stderr, "the cursor session reported no position\n");
        return 1;
    }

    printf("cursor position %d %d\n", cursor_x, cursor_y);

    struct ext_image_copy_capture_session_v1* session =
        ext_image_copy_capture_cursor_session_v1_get_capture_session(cursor);

    ext_image_copy_capture_session_v1_add_listener(session, &session_listener, NULL);

    while (!constraints_done && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (!cap_w || !cap_h) {
        fprintf(stderr, "the cursor session announced no buffer size\n");
        return 1;
    }

    printf("cursor session constraints %ux%u\n", cap_w, cap_h);

    // one session per cursor session, and the second one is a protocol error
    ext_image_copy_capture_cursor_session_v1_get_capture_session(cursor);

    return wl_expect_error("ext_image_copy_capture_cursor_session_v1",
                           EXT_IMAGE_COPY_CAPTURE_CURSOR_SESSION_V1_ERROR_DUPLICATE_SESSION);
}
