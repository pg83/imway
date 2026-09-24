#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <viewporter-client-protocol.h>
#include <tearing-control-v1-client-protocol.h>
#include <fifo-v1-client-protocol.h>
#include <commit-timing-v1-client-protocol.h>
#include <content-type-v1-client-protocol.h>
#include <alpha-modifier-v1-client-protocol.h>
#include <color-representation-v1-client-protocol.h>
#include <linux-dmabuf-v1-client-protocol.h>
#include <xdg-foreign-unstable-v2-client-protocol.h>
#include <xdg-toplevel-icon-v1-client-protocol.h>
#include <single-pixel-buffer-v1-client-protocol.h>
#include <security-context-v1-client-protocol.h>
#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>
#include <wlr-screencopy-unstable-v1-client-protocol.h>
#include <color-management-v1-client-protocol.h>
#include <xdg-toplevel-drag-v1-client-protocol.h>

static struct wl_compositor* compositor;
static struct wl_subcompositor* subcompositor;
static struct xdg_wm_base* wm_base;
static struct xdg_wm_base* wm_base3; // reposition arrived in version 3
static struct wl_seat* seat;
static struct wl_data_device_manager* data_manager;
static struct wl_shm* shm;
static struct wp_viewporter* viewporter;
static struct wp_tearing_control_manager_v1* tearing;
static struct wp_fifo_manager_v1* fifo_mgr;
static struct wp_commit_timing_manager_v1* timing;
static struct wp_content_type_manager_v1* content_type;
static struct wp_alpha_modifier_v1* alpha_mod;
static struct wp_color_representation_manager_v1* representation;
static struct zwp_linux_dmabuf_v1* dmabuf;
static struct zxdg_exporter_v2* exporter;
static struct xdg_toplevel_icon_manager_v1* icons;
static struct wp_single_pixel_buffer_manager_v1* spb;
static struct wp_security_context_manager_v1* security;
static struct ext_output_image_capture_source_manager_v1* cap_source;
static struct ext_image_copy_capture_manager_v1* cap_mgr;
static struct zwlr_screencopy_manager_v1* screencopy;
static struct wl_output* output;
static struct wp_color_manager_v1* colour;
static struct xdg_toplevel_drag_manager_v1* drag_mgr;

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data;
    (void)version;

    if (!strcmp(interface, wl_compositor_interface.name))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 6);
    else if (!strcmp(interface, wl_subcompositor_interface.name))
        subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        wm_base3 = wl_registry_bind(registry, name, &xdg_wm_base_interface, 3);
    }
    else if (!strcmp(interface, wl_seat_interface.name))
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
    else if (!strcmp(interface, wl_data_device_manager_interface.name))
        data_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3);
    else if (!strcmp(interface, wl_shm_interface.name))
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    else if (!strcmp(interface, wp_tearing_control_manager_v1_interface.name))
        tearing = wl_registry_bind(registry, name, &wp_tearing_control_manager_v1_interface, 1);
    else if (!strcmp(interface, wp_fifo_manager_v1_interface.name))
        fifo_mgr = wl_registry_bind(registry, name, &wp_fifo_manager_v1_interface, 1);
    else if (!strcmp(interface, wp_commit_timing_manager_v1_interface.name))
        timing = wl_registry_bind(registry, name, &wp_commit_timing_manager_v1_interface, 1);
    else if (!strcmp(interface, wp_content_type_manager_v1_interface.name))
        content_type = wl_registry_bind(registry, name, &wp_content_type_manager_v1_interface, 1);
    else if (!strcmp(interface, wp_alpha_modifier_v1_interface.name))
        alpha_mod = wl_registry_bind(registry, name, &wp_alpha_modifier_v1_interface, 1);
    else if (!strcmp(interface, wp_color_representation_manager_v1_interface.name))
        representation = wl_registry_bind(registry, name,
            &wp_color_representation_manager_v1_interface, 1);
    else if (!strcmp(interface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, 3);
    else if (!strcmp(interface, zxdg_exporter_v2_interface.name))
        exporter = wl_registry_bind(registry, name, &zxdg_exporter_v2_interface, 1);
    else if (!strcmp(interface, xdg_toplevel_icon_manager_v1_interface.name))
        icons = wl_registry_bind(registry, name, &xdg_toplevel_icon_manager_v1_interface, 1);
    else if (!strcmp(interface, wp_single_pixel_buffer_manager_v1_interface.name))
        spb = wl_registry_bind(registry, name,
            &wp_single_pixel_buffer_manager_v1_interface, 1);
    else if (!strcmp(interface, wp_security_context_manager_v1_interface.name))
        security = wl_registry_bind(registry, name,
            &wp_security_context_manager_v1_interface, 1);
    else if (!strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name))
        cap_source = wl_registry_bind(registry, name,
            &ext_output_image_capture_source_manager_v1_interface, 1);
    else if (!strcmp(interface, ext_image_copy_capture_manager_v1_interface.name))
        cap_mgr = wl_registry_bind(registry, name,
            &ext_image_copy_capture_manager_v1_interface, 1);
    else if (!strcmp(interface, zwlr_screencopy_manager_v1_interface.name))
        screencopy = wl_registry_bind(registry, name,
            &zwlr_screencopy_manager_v1_interface, 1);
    else if (!strcmp(interface, wl_output_interface.name) && !output)
        output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    else if (!strcmp(interface, wp_color_manager_v1_interface.name))
        colour = wl_registry_bind(registry, name, &wp_color_manager_v1_interface, 1);
    else if (!strcmp(interface, xdg_toplevel_drag_manager_v1_interface.name))
        drag_mgr = wl_registry_bind(registry, name,
            &xdg_toplevel_drag_manager_v1_interface, 1);
}

static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int expect_error(struct wl_display* display, const char* interface_name, uint32_t expected) {
    if (wl_display_roundtrip(display) >= 0) {
        fprintf(stderr, "request unexpectedly succeeded\n");
        return 1;
    }

    const struct wl_interface* interface = NULL;
    uint32_t object_id = 0;
    uint32_t code = wl_display_get_protocol_error(display, &interface, &object_id);

    // interface_name may be NULL: a request marked destructor takes the
    // proxy with it, and libwayland then has no object to name
    if (wl_display_get_error(display) != EPROTO || code != expected ||
        (interface_name && (!interface || strcmp(interface->name, interface_name)))) {
        fprintf(stderr, "unexpected protocol error: iface=%s id=%u code=%u errno=%d\n",
                interface ? interface->name : "(none)", object_id, code,
                wl_display_get_error(display));
        return 1;
    }

    return 0;
}

// a mapped pool to violate wl_shm with; size is the mapping the compositor
// is told about, which the bad-fd case deliberately cannot satisfy
static struct wl_shm_pool* make_pool(int size) {
    int fd = memfd_create("errors-shm", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        return NULL;
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);

    close(fd);

    return pool;
}

// a socket that is really listening, which is what the manager insists on
static int listening_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    // an abstract name, so nothing is left behind in the scratch directory
    snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "imway-errors-%d", getpid());

    socklen_t len = (socklen_t)(sizeof(addr.sun_family) + 1 + strlen(addr.sun_path + 1));

    if (bind(fd, (struct sockaddr*)&addr, len) < 0 || listen(fd, 1) < 0) {
        close(fd);

        return -1;
    }

    return fd;
}

// a context with all of its metadata in place and committed
// a context on a real listening socket with none of its metadata set yet
static struct wp_security_context_v1* fresh_context(void) {
    int listen_fd = listening_socket();
    int pair[2];

    if (listen_fd < 0 || socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) < 0) {
        return NULL;
    }

    struct wp_security_context_v1* ctx =
        wp_security_context_manager_v1_create_listener(security, listen_fd, pair[0]);

    close(listen_fd);
    close(pair[0]);
    close(pair[1]);

    return ctx;
}

static struct wp_security_context_v1* committed_context(void) {
    struct wp_security_context_v1* ctx = fresh_context();

    if (!ctx) {
        return NULL;
    }

    wp_security_context_v1_set_sandbox_engine(ctx, "imway.test");
    wp_security_context_v1_set_app_id(ctx, "imway.test.app");
    wp_security_context_v1_set_instance_id(ctx, "1");
    wp_security_context_v1_commit(ctx);

    return ctx;
}

static uint32_t cap_w, cap_h;
static int cap_done;

static void cap_buffer_size(void* d, struct ext_image_copy_capture_session_v1* s,
                            uint32_t w, uint32_t h) {
    (void)d; (void)s;
    cap_w = w;
    cap_h = h;
}
static void cap_shm_format(void* d, struct ext_image_copy_capture_session_v1* s, uint32_t f) {
    (void)d; (void)s; (void)f;
}
static void cap_dmabuf_device(void* d, struct ext_image_copy_capture_session_v1* s,
                              struct wl_array* a) {
    (void)d; (void)s; (void)a;
}
static void cap_dmabuf_format(void* d, struct ext_image_copy_capture_session_v1* s,
                              uint32_t f, struct wl_array* a) {
    (void)d; (void)s; (void)f; (void)a;
}
static void cap_session_done(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
    cap_done = 1;
}
static void cap_stopped(void* d, struct ext_image_copy_capture_session_v1* s) {
    (void)d; (void)s;
}
static const struct ext_image_copy_capture_session_v1_listener cap_session_listener = {
    .buffer_size = cap_buffer_size,
    .shm_format = cap_shm_format,
    .dmabuf_device = cap_dmabuf_device,
    .dmabuf_format = cap_dmabuf_format,
    .done = cap_session_done,
    .stopped = cap_stopped,
};

static struct ext_image_copy_capture_session_v1* capture_session(struct wl_display* display) {
    struct ext_image_capture_source_v1* src =
        ext_output_image_capture_source_manager_v1_create_source(cap_source, output);
    struct ext_image_copy_capture_session_v1* session =
        ext_image_copy_capture_manager_v1_create_session(cap_mgr, src, 0);

    ext_image_copy_capture_session_v1_add_listener(session, &cap_session_listener, NULL);

    while (!cap_done && wl_display_dispatch(display) != -1) {
    }

    return cap_done ? session : NULL;
}

// a buffer matching whatever the session announced, so the capture succeeds
// and the frame moves into the state the errors below are about
static struct wl_buffer* sized_buffer(uint32_t w, uint32_t h) {
    int stride = (int)w * 4, size = stride * (int)h;
    struct wl_shm_pool* pool = make_pool(size);

    if (!pool) {
        return NULL;
    }

    return wl_shm_pool_create_buffer(pool, 0, (int)w, (int)h, stride, WL_SHM_FORMAT_XRGB8888);
}

static struct ext_image_copy_capture_frame_v1* captured_frame(struct wl_display* display) {
    struct ext_image_copy_capture_session_v1* session = capture_session(display);

    if (!session) {
        return NULL;
    }

    struct wl_buffer* buf = sized_buffer(cap_w, cap_h);

    if (!buf) {
        return NULL;
    }

    struct ext_image_copy_capture_frame_v1* frame =
        ext_image_copy_capture_session_v1_create_frame(session);

    ext_image_copy_capture_frame_v1_attach_buffer(frame, buf);
    ext_image_copy_capture_frame_v1_capture(frame);

    return frame;
}

static uint32_t wlr_w, wlr_h, wlr_stride, wlr_format;
static int wlr_got_buffer;

static void wlr_buffer(void* d, struct zwlr_screencopy_frame_v1* f,
                       uint32_t format, uint32_t w, uint32_t h, uint32_t stride) {
    (void)d; (void)f;
    wlr_format = format;
    wlr_w = w;
    wlr_h = h;
    wlr_stride = stride;
    wlr_got_buffer = 1;
}
static void wlr_flags(void* d, struct zwlr_screencopy_frame_v1* f, uint32_t fl) {
    (void)d; (void)f; (void)fl;
}
static void wlr_ready(void* d, struct zwlr_screencopy_frame_v1* f,
                      uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)d; (void)f; (void)hi; (void)lo; (void)ns;
}
static void wlr_failed(void* d, struct zwlr_screencopy_frame_v1* f) { (void)d; (void)f; }
static void wlr_damage(void* d, struct zwlr_screencopy_frame_v1* f,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)x; (void)y; (void)w; (void)h;
}
static void wlr_dmabuf(void* d, struct zwlr_screencopy_frame_v1* f,
                       uint32_t fmt, uint32_t w, uint32_t h) {
    (void)d; (void)f; (void)fmt; (void)w; (void)h;
}
static void wlr_buffer_done(void* d, struct zwlr_screencopy_frame_v1* f) { (void)d; (void)f; }
static const struct zwlr_screencopy_frame_v1_listener wlr_listener = {
    .buffer = wlr_buffer,
    .flags = wlr_flags,
    .ready = wlr_ready,
    .failed = wlr_failed,
    .damage = wlr_damage,
    .linux_dmabuf = wlr_dmabuf,
    .buffer_done = wlr_buffer_done,
};

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }

    struct wl_display* display = wl_display_connect(NULL);

    if (!display) {
        return 2;
    }

    struct wl_registry* registry = wl_display_get_registry(display);

    wl_registry_add_listener(registry, &registry_listener, NULL);

    if (wl_display_roundtrip(display) < 0 || !compositor || !subcompositor || !wm_base ||
        !seat || !data_manager || !shm || !viewporter || !tearing || !fifo_mgr || !timing ||
        !content_type || !alpha_mod || !representation || !dmabuf || !exporter ||
        !icons || !spb || !wm_base3 || !security || !cap_source || !cap_mgr ||
        !screencopy || !output || !colour || !drag_mgr) {
        return 2;
    }

    struct wl_surface* surface = wl_compositor_create_surface(compositor);

    if (!strcmp(argv[1], "screencopy-twice")) {
        struct zwlr_screencopy_frame_v1* frame =
            zwlr_screencopy_manager_v1_capture_output(screencopy, 0, output);

        zwlr_screencopy_frame_v1_add_listener(frame, &wlr_listener, NULL);

        while (!wlr_got_buffer && wl_display_dispatch(display) != -1) {
        }

        int size = (int)wlr_stride * (int)wlr_h;
        struct wl_shm_pool* pool = make_pool(size);

        if (!pool) return 2;

        struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, (int)wlr_w, (int)wlr_h,
                                                          (int)wlr_stride, wlr_format);

        zwlr_screencopy_frame_v1_copy(frame, buf);
        zwlr_screencopy_frame_v1_copy(frame, buf);

        return expect_error(display, zwlr_screencopy_frame_v1_interface.name,
                            ZWLR_SCREENCOPY_FRAME_V1_ERROR_ALREADY_USED);
    }

    // a copy buffer must match the announced frame on every count; each
    // mode misses on exactly one
    if (!strncmp(argv[1], "screencopy-", 11)) {
        struct zwlr_screencopy_frame_v1* frame =
            zwlr_screencopy_manager_v1_capture_output(screencopy, 0, output);

        zwlr_screencopy_frame_v1_add_listener(frame, &wlr_listener, NULL);

        while (!wlr_got_buffer && wl_display_dispatch(display) != -1) {
        }

        int w = (int)wlr_w, h = (int)wlr_h, stride = (int)wlr_stride;
        struct wl_buffer* buf = NULL;

        if (!strcmp(argv[1], "screencopy-not-shm")) {
            buf = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(spb, 0, 0, 0, ~0u);
        } else if (!strcmp(argv[1], "screencopy-narrow")) {
            w--;
        } else if (!strcmp(argv[1], "screencopy-short")) {
            h--;
        } else if (!strcmp(argv[1], "screencopy-thin-stride")) {
            stride = w * 4 - 4;
        } else {
            return 2;
        }

        if (!buf) {
            struct wl_shm_pool* pool = make_pool(stride * h);

            if (!pool) return 2;

            buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, wlr_format);
        }

        zwlr_screencopy_frame_v1_copy(frame, buf);

        return expect_error(display, zwlr_screencopy_frame_v1_interface.name,
                            ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER);
    }

    if (!strcmp(argv[1], "drag-source-reused")) {
        struct wl_data_source* source = wl_data_device_manager_create_data_source(data_manager);

        xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag(drag_mgr, source);
        // the same source cannot carry a second drag object
        xdg_toplevel_drag_manager_v1_get_xdg_toplevel_drag(drag_mgr, source);

        return expect_error(display, xdg_toplevel_drag_manager_v1_interface.name,
                            XDG_TOPLEVEL_DRAG_MANAGER_V1_ERROR_INVALID_SOURCE);
    }

    // an ICC profile fd the compositor cannot read from: opened write-only
    if (!strcmp(argv[1], "colour-icc-write-only")) {
        char path[512];

        snprintf(path, sizeof(path), "%s/icc-write-only", getenv("XDG_RUNTIME_DIR"));

        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

        if (fd < 0 || write(fd, "icc", 3) != 3) {
            return 2;
        }

        struct wp_image_description_creator_icc_v1* icc = wp_color_manager_v1_create_icc_creator(colour);

        wp_image_description_creator_icc_v1_set_icc_file(icc, fd, 0, 3);
        close(fd);

        return expect_error(display, wp_image_description_creator_icc_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_ICC_V1_ERROR_BAD_FD);
    }

    if (!strcmp(argv[1], "colour-primaries-twice")) {
        struct wp_image_description_creator_params_v1* params =
            wp_color_manager_v1_create_parametric_creator(colour);

        wp_image_description_creator_params_v1_set_primaries(
            params, 680000, 320000, 265000, 690000, 150000, 60000, 312700, 329000);
        wp_image_description_creator_params_v1_set_primaries(
            params, 640000, 330000, 300000, 600000, 150000, 60000, 312700, 329000);

        return expect_error(display, wp_image_description_creator_params_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_ALREADY_SET);
    }

    // parametric descriptions refused at create: primaries missing, a
    // luminance range upside down once the transfer function is known, and
    // content light levels outside the range or the wrong way round
    if (!strncmp(argv[1], "colour-create-", 14)) {
        const char* what = argv[1] + 14;
        struct wp_image_description_creator_params_v1* params =
            wp_color_manager_v1_create_parametric_creator(colour);
        uint32_t code = WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE;

        if (!strcmp(what, "lum-before-tf")) {
            // valid against the reference when set, but max is below min
            // once create sees a transfer function that is not PQ
            wp_image_description_creator_params_v1_set_luminances(params, 100000, 5, 20);
        }

        wp_image_description_creator_params_v1_set_tf_named(
            params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22);

        if (!strcmp(what, "no-primaries")) {
            code = WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INCOMPLETE_SET;
        } else {
            wp_image_description_creator_params_v1_set_primaries_named(
                params, WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
        }

        if (!strncmp(what, "cll", 3) || !strncmp(what, "fall", 4)) {
            wp_image_description_creator_params_v1_set_luminances(params, 1, 1000, 203);
        }

        if (!strcmp(what, "cll-over-max")) {
            wp_image_description_creator_params_v1_set_max_cll(params, 2000);
        } else if (!strcmp(what, "fall-over-max")) {
            wp_image_description_creator_params_v1_set_max_fall(params, 2000);
        } else if (!strcmp(what, "fall-over-cll")) {
            wp_image_description_creator_params_v1_set_max_cll(params, 300);
            wp_image_description_creator_params_v1_set_max_fall(params, 500);
        } else if (strcmp(what, "no-primaries") && strcmp(what, "lum-before-tf")) {
            return 2;
        }

        wp_image_description_creator_params_v1_create(params);

        // create is a destructor: the object the error names is gone here
        return expect_error(display, NULL, code);
    }

    // a failed description cannot be set on a surface
    if (!strcmp(argv[1], "colour-set-failed")) {
        struct wp_color_management_surface_v1* cms = wp_color_manager_v1_get_surface(colour, surface);
        struct wp_image_description_creator_params_v1* params =
            wp_color_manager_v1_create_parametric_creator(colour);

        wp_image_description_creator_params_v1_set_tf_named(
            params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22);
        // a primary with zero y: the description fails
        wp_image_description_creator_params_v1_set_primaries(
            params, 640000, 0, 300000, 600000, 150000, 60000, 312700, 329000);

        struct wp_image_description_v1* desc = wp_image_description_creator_params_v1_create(params);

        wl_display_roundtrip(display);
        wp_color_management_surface_v1_set_image_description(cms, desc, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);

        return expect_error(display, wp_color_management_surface_v1_interface.name,
                            WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_IMAGE_DESCRIPTION);
    }

    if (!strcmp(argv[1], "colour-bad-luminance")) {
        struct wp_image_description_creator_params_v1* params =
            wp_color_manager_v1_create_parametric_creator(colour);

        wp_image_description_creator_params_v1_set_tf_named(
            params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
        wp_image_description_creator_params_v1_set_primaries_named(
            params, WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);
        // a floor above the ceiling
        wp_image_description_creator_params_v1_set_luminances(params, 1000000, 1, 1);
        wp_image_description_creator_params_v1_create(params);

        // create is a destructor, so the object the error names is already
        // gone on this side
        return expect_error(display, NULL,
                            WP_IMAGE_DESCRIPTION_CREATOR_PARAMS_V1_ERROR_INVALID_LUMINANCE);
    }

    if (!strcmp(argv[1], "colour-info-failed")) {
        struct wp_image_description_creator_params_v1* params =
            wp_color_manager_v1_create_parametric_creator(colour);

        wp_image_description_creator_params_v1_set_tf_named(
            params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22);
        // a primary with zero y: the description fails instead of becoming
        // ready, and a failed one is never ready to be asked about
        wp_image_description_creator_params_v1_set_primaries(
            params, 640000, 0, 300000, 600000, 150000, 60000, 312700, 329000);

        struct wp_image_description_v1* desc = wp_image_description_creator_params_v1_create(params);

        wl_display_roundtrip(display);
        wp_image_description_v1_get_information(desc);

        return expect_error(display, wp_image_description_v1_interface.name,
                            WP_IMAGE_DESCRIPTION_V1_ERROR_NOT_READY);
    }

    if (!strcmp(argv[1], "colour-surface-dead")) {
        struct wp_color_management_surface_v1* cms =
            wp_color_manager_v1_get_surface(colour, surface);
        struct wp_image_description_creator_params_v1* params =
            wp_color_manager_v1_create_parametric_creator(colour);

        wp_image_description_creator_params_v1_set_tf_named(
            params, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB);
        wp_image_description_creator_params_v1_set_primaries_named(
            params, WP_COLOR_MANAGER_V1_PRIMARIES_SRGB);

        struct wp_image_description_v1* desc =
            wp_image_description_creator_params_v1_create(params);

        wl_display_roundtrip(display);
        wl_surface_destroy(surface);
        wp_color_management_surface_v1_set_image_description(
            cms, desc, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);

        return expect_error(display, wp_color_management_surface_v1_interface.name,
                            WP_COLOR_MANAGEMENT_SURFACE_V1_ERROR_INERT);
    }

    if (!strcmp(argv[1], "capture-bad-option")) {
        struct ext_image_capture_source_v1* src =
            ext_output_image_capture_source_manager_v1_create_source(cap_source, output);

        // only paint_cursors is defined
        ext_image_copy_capture_manager_v1_create_session(cap_mgr, src, 0xfu);

        return expect_error(display, ext_image_copy_capture_manager_v1_interface.name,
                            EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_ERROR_INVALID_OPTION);
    }

    // every edge of the damage rectangle is checked on its own: a zero
    // width, a negative origin on either axis, a zero height
    static const struct {
        const char* mode;
        int32_t x, y, w, h;
    } bad_damage[] = {
        {"capture-bad-damage", 0, 0, 0, 0},
        {"capture-damage-left", -1, 0, 1, 1},
        {"capture-damage-above", 0, -1, 1, 1},
        {"capture-damage-flat", 0, 0, 1, 0},
    };

    for (size_t i = 0; i < sizeof(bad_damage) / sizeof(bad_damage[0]); i++) {
        if (strcmp(argv[1], bad_damage[i].mode)) {
            continue;
        }

        struct ext_image_copy_capture_session_v1* session = capture_session(display);

        if (!session) return 2;

        struct ext_image_copy_capture_frame_v1* frame =
            ext_image_copy_capture_session_v1_create_frame(session);

        ext_image_copy_capture_frame_v1_damage_buffer(frame, bad_damage[i].x, bad_damage[i].y,
                                                      bad_damage[i].w, bad_damage[i].h);

        return expect_error(display, ext_image_copy_capture_frame_v1_interface.name,
                            EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_INVALID_BUFFER_DAMAGE);
    }

    if (!strcmp(argv[1], "capture-attach-after")) {
        struct ext_image_copy_capture_frame_v1* frame = captured_frame(display);

        if (!frame) return 2;

        ext_image_copy_capture_frame_v1_attach_buffer(frame, sized_buffer(cap_w, cap_h));

        return expect_error(display, ext_image_copy_capture_frame_v1_interface.name,
                            EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED);
    }

    if (!strcmp(argv[1], "capture-damage-after")) {
        struct ext_image_copy_capture_frame_v1* frame = captured_frame(display);

        if (!frame) return 2;

        ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, 8, 8);

        return expect_error(display, ext_image_copy_capture_frame_v1_interface.name,
                            EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED);
    }

    if (!strcmp(argv[1], "capture-twice")) {
        struct ext_image_copy_capture_frame_v1* frame = captured_frame(display);

        if (!frame) return 2;

        ext_image_copy_capture_frame_v1_capture(frame);

        return expect_error(display, ext_image_copy_capture_frame_v1_interface.name,
                            EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_ALREADY_CAPTURED);
    }

    if (!strcmp(argv[1], "security-bad-listen-fd")) {
        int pair[2];

        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) < 0) return 2;

        // a connected socket, not a listening one
        wp_security_context_manager_v1_create_listener(security, pair[0], pair[1]);
        close(pair[0]);
        close(pair[1]);

        return expect_error(display, wp_security_context_manager_v1_interface.name,
                            WP_SECURITY_CONTEXT_MANAGER_V1_ERROR_INVALID_LISTEN_FD);
    }

    if (!strcmp(argv[1], "security-incomplete")) {
        int listen_fd = listening_socket();
        int pair[2];

        if (listen_fd < 0 || socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) < 0) return 2;

        struct wp_security_context_v1* ctx =
            wp_security_context_manager_v1_create_listener(security, listen_fd, pair[0]);

        close(listen_fd);
        close(pair[0]);
        close(pair[1]);
        // committed without an engine, an app id or an instance id
        wp_security_context_v1_commit(ctx);

        return expect_error(display, wp_security_context_v1_interface.name,
                            WP_SECURITY_CONTEXT_V1_ERROR_INVALID_METADATA);
    }

    if (!strcmp(argv[1], "security-listen-not-socket")) {
        int fd = memfd_create("not-a-socket", 0);
        int pair[2];

        if (fd < 0 || socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) < 0) return 2;

        // not a socket at all: SO_ACCEPTCONN cannot even be asked
        wp_security_context_manager_v1_create_listener(security, fd, pair[1]);
        close(fd);
        close(pair[0]);
        close(pair[1]);

        return expect_error(display, wp_security_context_manager_v1_interface.name,
                            WP_SECURITY_CONTEXT_MANAGER_V1_ERROR_INVALID_LISTEN_FD);
    }

    // each piece of metadata is required on its own, and each can be set
    // only once
    if (!strcmp(argv[1], "security-no-app-id") || !strcmp(argv[1], "security-no-instance")) {
        struct wp_security_context_v1* ctx = fresh_context();

        if (!ctx) return 2;

        wp_security_context_v1_set_sandbox_engine(ctx, "imway.test");
        if (!strcmp(argv[1], "security-no-instance"))
            wp_security_context_v1_set_app_id(ctx, "imway.test.app");
        wp_security_context_v1_commit(ctx);

        return expect_error(display, wp_security_context_v1_interface.name,
                            WP_SECURITY_CONTEXT_V1_ERROR_INVALID_METADATA);
    }

    if (!strcmp(argv[1], "security-engine-twice") || !strcmp(argv[1], "security-app-id-twice") ||
        !strcmp(argv[1], "security-instance-twice")) {
        struct wp_security_context_v1* ctx = fresh_context();

        if (!ctx) return 2;

        for (int i = 0; i < 2; i++) {
            if (!strcmp(argv[1], "security-engine-twice"))
                wp_security_context_v1_set_sandbox_engine(ctx, "imway.test");
            else if (!strcmp(argv[1], "security-app-id-twice"))
                wp_security_context_v1_set_app_id(ctx, "imway.test.app");
            else
                wp_security_context_v1_set_instance_id(ctx, "1");
        }

        return expect_error(display, wp_security_context_v1_interface.name,
                            WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_SET);
    }

    if (!strcmp(argv[1], "security-engine-after-commit")) {
        struct wp_security_context_v1* ctx = committed_context();

        if (!ctx) return 2;

        wp_security_context_v1_set_sandbox_engine(ctx, "imway.late");

        return expect_error(display, wp_security_context_v1_interface.name,
                            WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED);
    }

    if (!strcmp(argv[1], "security-appid-after-commit")) {
        struct wp_security_context_v1* ctx = committed_context();

        if (!ctx) return 2;

        wp_security_context_v1_set_app_id(ctx, "imway.late.app");

        return expect_error(display, wp_security_context_v1_interface.name,
                            WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED);
    }

    if (!strcmp(argv[1], "security-instance-after-commit")) {
        struct wp_security_context_v1* ctx = committed_context();

        if (!ctx) return 2;

        wp_security_context_v1_set_instance_id(ctx, "2");

        return expect_error(display, wp_security_context_v1_interface.name,
                            WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED);
    }

    if (!strcmp(argv[1], "security-commit-twice")) {
        struct wp_security_context_v1* ctx = committed_context();

        if (!ctx) return 2;

        wp_security_context_v1_commit(ctx);

        return expect_error(display, wp_security_context_v1_interface.name,
                            WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED);
    }

    if (!strcmp(argv[1], "negative-max-size")) {
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
        struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);

        xdg_toplevel_set_max_size(tl, -1, 10);

        return expect_error(display, xdg_toplevel_interface.name,
                            XDG_TOPLEVEL_ERROR_INVALID_SIZE);
    }

    if (!strcmp(argv[1], "dmabuf-params-plane-gap")) {
        struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
        int fd = memfd_create("errors-plane", 0);

        if (fd < 0 || ftruncate(fd, 16 * 16 * 4) < 0) return 2;

        // planes 0 and 2, so plane 1 is a hole
        zwp_linux_buffer_params_v1_add(params, fd, 0, 0, 16 * 4, 0, 0);
        zwp_linux_buffer_params_v1_add(params, fd, 2, 0, 16 * 4, 0, 0);
        close(fd);
        zwp_linux_buffer_params_v1_create(params, 16, 16, 0x34325258u, 0);

        return expect_error(display, zwp_linux_buffer_params_v1_interface.name,
                            ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE);
    }

    if (!strcmp(argv[1], "export-plain-surface")) {
        // a surface that never took an xdg_toplevel role
        zxdg_exporter_v2_export_toplevel(exporter, surface);

        return expect_error(display, zxdg_exporter_v2_interface.name,
                            ZXDG_EXPORTER_V2_ERROR_INVALID_SURFACE);
    }

    if (!strcmp(argv[1], "icon-not-shm")) {
        struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icons);
        struct wl_buffer* buf = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
            spb, 0xffffffffu, 0, 0, 0xffffffffu);

        xdg_toplevel_icon_v1_add_buffer(icon, buf, 1);

        return expect_error(display, xdg_toplevel_icon_v1_interface.name,
                            XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER);
    }

    if (!strcmp(argv[1], "icon-not-square")) {
        struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icons);
        struct wl_shm_pool* pool = make_pool(64 * 32 * 4);
        struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, 64, 32, 64 * 4,
                                                          WL_SHM_FORMAT_ARGB8888);

        xdg_toplevel_icon_v1_add_buffer(icon, buf, 1);

        return expect_error(display, xdg_toplevel_icon_v1_interface.name,
                            XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER);
    }

    // an icon buffer square and shm but wrong on one count: its scale, its
    // stride
    if (!strcmp(argv[1], "icon-zero-scale") || !strcmp(argv[1], "icon-thin-stride")) {
        struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icons);
        int thin = !strcmp(argv[1], "icon-thin-stride");
        int stride = thin ? 32 * 4 - 4 : 32 * 4;
        struct wl_shm_pool* pool = make_pool(stride * 32);

        if (!pool) return 2;

        struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, 32, 32, stride, WL_SHM_FORMAT_ARGB8888);

        xdg_toplevel_icon_v1_add_buffer(icon, buf, !strcmp(argv[1], "icon-zero-scale") ? 0 : 1);

        return expect_error(display, xdg_toplevel_icon_v1_interface.name,
                            XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER);
    }

    // the pool's file shrank under the compositor's mapping: copying the
    // icon out faults, and the buffer's owner is told
    if (!strcmp(argv[1], "icon-sigbus")) {
        struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icons);
        int size = 32 * 32 * 4;
        int fd = memfd_create("errors-icon", 0);

        if (fd < 0 || ftruncate(fd, size) < 0) return 2;

        struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
        struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, 32, 32, 32 * 4, WL_SHM_FORMAT_ARGB8888);

        if (ftruncate(fd, 0) < 0) return 2;

        close(fd);
        xdg_toplevel_icon_v1_add_buffer(icon, buf, 1);

        return expect_error(display, wl_buffer_interface.name, WL_SHM_ERROR_INVALID_FD);
    }

    if (!strcmp(argv[1], "reposition-bad-positioner")) {
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wm_base3, surface);
        struct xdg_toplevel* parent_tl = xdg_surface_get_toplevel(parent_xs);
        struct wl_surface* child = wl_compositor_create_surface(compositor);
        struct xdg_surface* child_xs = xdg_wm_base_get_xdg_surface(wm_base3, child);
        struct xdg_positioner* good = xdg_wm_base_create_positioner(wm_base3);
        struct xdg_positioner* bad = xdg_wm_base_create_positioner(wm_base3);

        (void)parent_tl;
        xdg_positioner_set_size(good, 40, 40);
        xdg_positioner_set_anchor_rect(good, 0, 0, 10, 10);

        struct xdg_popup* popup = xdg_surface_get_popup(child_xs, parent_xs, good);

        // the second positioner was never given a size
        xdg_popup_reposition(popup, bad, 1);

        return expect_error(display, xdg_wm_base_interface.name,
                            XDG_WM_BASE_ERROR_INVALID_POSITIONER);
    }

    if (!strcmp(argv[1], "content-type-twice")) {
        wp_content_type_manager_v1_get_surface_content_type(content_type, surface);
        wp_content_type_manager_v1_get_surface_content_type(content_type, surface);

        return expect_error(display, wp_content_type_manager_v1_interface.name,
                            WP_CONTENT_TYPE_MANAGER_V1_ERROR_ALREADY_CONSTRUCTED);
    }

    if (!strcmp(argv[1], "alpha-mod-twice")) {
        wp_alpha_modifier_v1_get_surface(alpha_mod, surface);
        wp_alpha_modifier_v1_get_surface(alpha_mod, surface);

        return expect_error(display, wp_alpha_modifier_v1_interface.name,
                            WP_ALPHA_MODIFIER_V1_ERROR_ALREADY_CONSTRUCTED);
    }

    if (!strcmp(argv[1], "alpha-mod-dead-surface")) {
        struct wp_alpha_modifier_surface_v1* am =
            wp_alpha_modifier_v1_get_surface(alpha_mod, surface);

        wl_surface_destroy(surface);
        wp_alpha_modifier_surface_v1_set_multiplier(am, 0x80000000u);

        return expect_error(display, wp_alpha_modifier_surface_v1_interface.name,
                            WP_ALPHA_MODIFIER_SURFACE_V1_ERROR_NO_SURFACE);
    }

    if (!strcmp(argv[1], "dmabuf-params-incomplete")) {
        struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);

        // no plane was ever added
        zwp_linux_buffer_params_v1_create(params, 16, 16, 0x34325258u, 0);

        return expect_error(display, zwp_linux_buffer_params_v1_interface.name,
                            ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE);
    }

    if (!strcmp(argv[1], "representation-dead-alpha")) {
        struct wp_color_representation_surface_v1* cr =
            wp_color_representation_manager_v1_get_surface(representation, surface);

        wl_surface_destroy(surface);
        wp_color_representation_surface_v1_set_alpha_mode(cr, 0);

        return expect_error(display, wp_color_representation_surface_v1_interface.name,
                            WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_INERT);
    }

    if (!strcmp(argv[1], "representation-dead-coefficients")) {
        struct wp_color_representation_surface_v1* cr =
            wp_color_representation_manager_v1_get_surface(representation, surface);

        wl_surface_destroy(surface);
        wp_color_representation_surface_v1_set_coefficients_and_range(cr, 0, 0);

        return expect_error(display, wp_color_representation_surface_v1_interface.name,
                            WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_INERT);
    }

    if (!strcmp(argv[1], "representation-dead-chroma")) {
        struct wp_color_representation_surface_v1* cr =
            wp_color_representation_manager_v1_get_surface(representation, surface);

        wl_surface_destroy(surface);
        wp_color_representation_surface_v1_set_chroma_location(cr, 0);

        return expect_error(display, wp_color_representation_surface_v1_interface.name,
                            WP_COLOR_REPRESENTATION_SURFACE_V1_ERROR_INERT);
    }

    if (!strcmp(argv[1], "shm-bad-format")) {
        struct wl_shm_pool* pool = make_pool(64 * 64 * 4);

        wl_shm_pool_create_buffer(pool, 0, 64, 64, 64 * 4, WL_SHM_FORMAT_RGB565);

        return expect_error(display, wl_shm_pool_interface.name, WL_SHM_ERROR_INVALID_FORMAT);
    }

    if (!strcmp(argv[1], "shm-bad-stride")) {
        struct wl_shm_pool* pool = make_pool(64 * 64 * 4);

        // a stride narrower than the row it has to carry
        wl_shm_pool_create_buffer(pool, 0, 64, 64, 4, WL_SHM_FORMAT_XRGB8888);

        return expect_error(display, wl_shm_pool_interface.name, WL_SHM_ERROR_INVALID_STRIDE);
    }

    if (!strcmp(argv[1], "shm-pool-shrink")) {
        struct wl_shm_pool* pool = make_pool(4096);

        wl_shm_pool_resize(pool, 1024);

        return expect_error(display, wl_shm_pool_interface.name, WL_SHM_ERROR_INVALID_FD);
    }

    if (!strcmp(argv[1], "shm-pool-zero")) {
        int fd = memfd_create("errors-shm", 0);

        if (fd < 0) return 2;

        wl_shm_create_pool(shm, fd, 0);
        close(fd);

        return expect_error(display, wl_shm_interface.name, WL_SHM_ERROR_INVALID_STRIDE);
    }

    if (!strcmp(argv[1], "shm-pool-badfd")) {
        int pipefd[2];

        if (pipe(pipefd) < 0) return 2;

        // a pipe cannot be mapped, so the pool the compositor is asked for
        // cannot exist
        wl_shm_create_pool(shm, pipefd[0], 4096);
        close(pipefd[0]);
        close(pipefd[1]);

        return expect_error(display, wl_shm_interface.name, WL_SHM_ERROR_INVALID_FD);
    }

    if (!strcmp(argv[1], "attach-offset")) {
        // v5 moved the attach offset to wl_surface.offset
        wl_surface_attach(surface, NULL, 1, 0);

        return expect_error(display, wl_surface_interface.name,
                            WL_SURFACE_ERROR_INVALID_OFFSET);
    }

    if (!strcmp(argv[1], "subsurface-place-stranger")) {
        struct wl_surface* child = wl_compositor_create_surface(compositor);
        struct wl_surface* stranger = wl_compositor_create_surface(compositor);
        struct wl_subsurface* sub =
            wl_subcompositor_get_subsurface(subcompositor, child, surface);

        wl_subsurface_place_below(sub, stranger);

        return expect_error(display, wl_subsurface_interface.name,
                            WL_SUBSURFACE_ERROR_BAD_SURFACE);
    }

    if (!strcmp(argv[1], "positioner-zero-anchor")) {
        struct xdg_positioner* pos = xdg_wm_base_create_positioner(wm_base);

        xdg_positioner_set_anchor_rect(pos, 0, 0, 0, 0);

        return expect_error(display, xdg_positioner_interface.name,
                            XDG_POSITIONER_ERROR_INVALID_INPUT);
    }

    if (!strcmp(argv[1], "popup-bad-positioner")) {
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
        struct xdg_toplevel* parent_tl = xdg_surface_get_toplevel(parent_xs);
        struct wl_surface* child = wl_compositor_create_surface(compositor);
        struct xdg_surface* child_xs = xdg_wm_base_get_xdg_surface(wm_base, child);
        struct xdg_positioner* pos = xdg_wm_base_create_positioner(wm_base);

        (void)parent_tl;
        // never given a size or an anchor rectangle
        xdg_surface_get_popup(child_xs, parent_xs, pos);

        return expect_error(display, xdg_wm_base_interface.name,
                            XDG_WM_BASE_ERROR_INVALID_POSITIONER);
    }

    if (!strcmp(argv[1], "popup-parent-no-role")) {
        struct xdg_surface* parent_xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
        struct wl_surface* child = wl_compositor_create_surface(compositor);
        struct xdg_surface* child_xs = xdg_wm_base_get_xdg_surface(wm_base, child);
        struct xdg_positioner* pos = xdg_wm_base_create_positioner(wm_base);

        xdg_positioner_set_size(pos, 40, 40);
        xdg_positioner_set_anchor_rect(pos, 0, 0, 10, 10);
        // the parent xdg_surface never became a toplevel or a popup
        xdg_surface_get_popup(child_xs, parent_xs, pos);

        return expect_error(display, xdg_wm_base_interface.name,
                            XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT);
    }

    if (!strcmp(argv[1], "xdg-double-role")) {
        struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
        struct xdg_toplevel* tl = xdg_surface_get_toplevel(xs);
        struct xdg_positioner* pos = xdg_wm_base_create_positioner(wm_base);

        (void)tl;
        xdg_positioner_set_size(pos, 40, 40);
        xdg_positioner_set_anchor_rect(pos, 0, 0, 10, 10);
        xdg_surface_get_popup(xs, NULL, pos);

        return expect_error(display, xdg_surface_interface.name,
                            XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED);
    }

    if (!strcmp(argv[1], "viewport-dead-source")) {
        struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, surface);

        wl_surface_destroy(surface);
        wp_viewport_set_source(vp, wl_fixed_from_int(0), wl_fixed_from_int(0),
                               wl_fixed_from_int(8), wl_fixed_from_int(8));

        return expect_error(display, wp_viewport_interface.name,
                            WP_VIEWPORT_ERROR_NO_SURFACE);
    }

    if (!strcmp(argv[1], "tearing-twice")) {
        wp_tearing_control_manager_v1_get_tearing_control(tearing, surface);
        wp_tearing_control_manager_v1_get_tearing_control(tearing, surface);

        return expect_error(display, wp_tearing_control_manager_v1_interface.name,
                            WP_TEARING_CONTROL_MANAGER_V1_ERROR_TEARING_CONTROL_EXISTS);
    }

    if (!strcmp(argv[1], "fifo-dead-surface")) {
        struct wp_fifo_v1* f = wp_fifo_manager_v1_get_fifo(fifo_mgr, surface);

        wl_surface_destroy(surface);
        wp_fifo_v1_wait_barrier(f);

        return expect_error(display, wp_fifo_v1_interface.name,
                            WP_FIFO_V1_ERROR_SURFACE_DESTROYED);
    }

    if (!strcmp(argv[1], "commit-timer-dead-surface")) {
        struct wp_commit_timer_v1* t = wp_commit_timing_manager_v1_get_timer(timing, surface);

        wl_surface_destroy(surface);
        wp_commit_timer_v1_set_timestamp(t, 0, 1, 0);

        return expect_error(display, wp_commit_timer_v1_interface.name,
                            WP_COMMIT_TIMER_V1_ERROR_SURFACE_DESTROYED);
    }

    if (!strcmp(argv[1], "self-subsurface")) {
        wl_subcompositor_get_subsurface(subcompositor, surface, surface);

        return expect_error(display, wl_subcompositor_interface.name,
                            WL_SUBCOMPOSITOR_ERROR_BAD_PARENT);
    }

    if (!strcmp(argv[1], "invalid-transform")) {
        wl_surface_set_buffer_transform(surface, 99);

        return expect_error(display, wl_surface_interface.name,
                            WL_SURFACE_ERROR_INVALID_TRANSFORM);
    }

    if (!strcmp(argv[1], "defunct-subsurface")) {
        struct wl_surface* child = wl_compositor_create_surface(compositor);
        struct wl_subsurface* sub =
            wl_subcompositor_get_subsurface(subcompositor, child, surface);

        wl_surface_destroy(child);
        wl_subsurface_set_desync(sub);

        return wl_display_roundtrip(display) < 0;
    }

    if (!strcmp(argv[1], "invalid-dnd-mask")) {
        struct wl_data_source* source = wl_data_device_manager_create_data_source(data_manager);

        wl_data_source_set_actions(source, 1u << 31);

        return expect_error(display, wl_data_source_interface.name,
                            WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK);
    }

    if (!strcmp(argv[1], "duplicate-dnd-actions")) {
        struct wl_data_source* source = wl_data_device_manager_create_data_source(data_manager);

        wl_data_source_set_actions(source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        wl_data_source_set_actions(source, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);

        return expect_error(display, wl_data_source_interface.name,
                            WL_DATA_SOURCE_ERROR_INVALID_SOURCE);
    }

    struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm_base, surface);

    if (!strcmp(argv[1], "destroy-wm-base")) {
        wl_proxy_marshal_flags((struct wl_proxy*)wm_base, XDG_WM_BASE_DESTROY, NULL,
                               wl_proxy_get_version((struct wl_proxy*)wm_base), 0);

        return expect_error(display, xdg_wm_base_interface.name,
                            XDG_WM_BASE_ERROR_DEFUNCT_SURFACES);
    }

    if (!strcmp(argv[1], "duplicate-xdg")) {
        xdg_wm_base_get_xdg_surface(wm_base, surface);

        return expect_error(display, xdg_wm_base_interface.name, XDG_WM_BASE_ERROR_ROLE);
    }

    if (!strcmp(argv[1], "invalid-configure")) {
        xdg_surface_get_toplevel(xs);
        xdg_surface_ack_configure(xs, 0xdeadbeef);

        return expect_error(display, xdg_surface_interface.name,
                            XDG_SURFACE_ERROR_INVALID_SERIAL);
    }

    if (!strcmp(argv[1], "invalid-resize-edge")) {
        struct xdg_toplevel* toplevel = xdg_surface_get_toplevel(xs);

        xdg_toplevel_resize(toplevel, seat, 1, 3);

        return expect_error(display, xdg_toplevel_interface.name,
                            XDG_TOPLEVEL_ERROR_INVALID_RESIZE_EDGE);
    }

    if (!strcmp(argv[1], "negative-min-size")) {
        struct xdg_toplevel* toplevel = xdg_surface_get_toplevel(xs);

        xdg_toplevel_set_min_size(toplevel, -1, 1);

        return expect_error(display, xdg_toplevel_interface.name,
                            XDG_TOPLEVEL_ERROR_INVALID_SIZE);
    }

    if (!strcmp(argv[1], "conflicting-size")) {
        struct xdg_toplevel* toplevel = xdg_surface_get_toplevel(xs);

        xdg_toplevel_set_min_size(toplevel, 100, 100);
        xdg_toplevel_set_max_size(toplevel, 50, 50);
        wl_surface_commit(surface);

        return expect_error(display, xdg_toplevel_interface.name,
                            XDG_TOPLEVEL_ERROR_INVALID_SIZE);
    }

    if (!strcmp(argv[1], "unmapped-popup-parent")) {
        xdg_surface_get_toplevel(xs);
        struct wl_surface* popup_surface = wl_compositor_create_surface(compositor);
        struct xdg_surface* popup_xs = xdg_wm_base_get_xdg_surface(wm_base, popup_surface);
        struct xdg_positioner* positioner = xdg_wm_base_create_positioner(wm_base);

        xdg_positioner_set_size(positioner, 10, 10);
        xdg_positioner_set_anchor_rect(positioner, 0, 0, 10, 10);
        xdg_surface_get_popup(popup_xs, xs, positioner);
        wl_surface_commit(popup_surface);

        return expect_error(display, xdg_wm_base_interface.name,
                            XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT);
    }

    if (!strcmp(argv[1], "roleless-popup-parent")) {
        // the parent's toplevel and xdg_surface go before the popup's first
        // commit: its wl_surface lives on, but is no xdg_surface to map on
        struct xdg_toplevel* parent_toplevel = xdg_surface_get_toplevel(xs);
        struct wl_surface* popup_surface = wl_compositor_create_surface(compositor);
        struct xdg_surface* popup_xs = xdg_wm_base_get_xdg_surface(wm_base, popup_surface);
        struct xdg_positioner* positioner = xdg_wm_base_create_positioner(wm_base);

        xdg_positioner_set_size(positioner, 10, 10);
        xdg_positioner_set_anchor_rect(positioner, 0, 0, 10, 10);
        xdg_surface_get_popup(popup_xs, xs, positioner);
        xdg_toplevel_destroy(parent_toplevel);
        xdg_surface_destroy(xs);
        wl_surface_commit(popup_surface);

        return expect_error(display, xdg_wm_base_interface.name,
                            XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT);
    }

    if (!strcmp(argv[1], "incomplete-positioner")) {
        xdg_surface_get_toplevel(xs);
        struct wl_surface* popup_surface = wl_compositor_create_surface(compositor);
        struct xdg_surface* popup_xs = xdg_wm_base_get_xdg_surface(wm_base, popup_surface);
        struct xdg_positioner* positioner = xdg_wm_base_create_positioner(wm_base);

        xdg_positioner_set_size(positioner, 10, 10);
        xdg_surface_get_popup(popup_xs, xs, positioner);

        return expect_error(display, xdg_wm_base_interface.name,
                            XDG_WM_BASE_ERROR_INVALID_POSITIONER);
    }

    return 2;
}
