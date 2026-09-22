// Walks one chain up to a resource the scenario's IMWAY_CHAOS makes the
// compositor fail to allocate, and checks the client is told: a wl_display
// no_memory error. The globals of the second half of the protocol table and
// the objects their requests make, from capture to colour management.
//   usage: client_resource_fault_late bind IFACE  — bind the global IFACE
//          client_resource_fault_late MODE        — run the request chain
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <wayland-client.h>
#include <alpha-modifier-v1-client-protocol.h>
#include <color-management-v1-client-protocol.h>
#include <color-representation-v1-client-protocol.h>
#include <content-type-v1-client-protocol.h>
#include <cursor-shape-v1-client-protocol.h>
#include <ext-idle-notify-v1-client-protocol.h>
#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>
#include <fractional-scale-v1-client-protocol.h>
#include <idle-inhibit-unstable-v1-client-protocol.h>
#include <keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h>
#include <linux-dmabuf-v1-client-protocol.h>
#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <pointer-gestures-unstable-v1-client-protocol.h>
#include <presentation-time-client-protocol.h>
#include <relative-pointer-unstable-v1-client-protocol.h>
#include <security-context-v1-client-protocol.h>
#include <single-pixel-buffer-v1-client-protocol.h>
#include <wlr-screencopy-unstable-v1-client-protocol.h>
#include <xdg-activation-v1-client-protocol.h>
#include <xdg-output-unstable-v1-client-protocol.h>
#include <xdg-toplevel-icon-v1-client-protocol.h>

static struct {
    const struct wl_interface* iface;
    uint32_t version;
    void* proxy;
} globals[] = {
    {&wl_compositor_interface, 4, NULL},
    {&wl_seat_interface, 5, NULL},
    {&wl_output_interface, 1, NULL},
    {&wp_security_context_manager_v1_interface, 1, NULL},
    {&ext_output_image_capture_source_manager_v1_interface, 1, NULL},
    {&ext_image_copy_capture_manager_v1_interface, 1, NULL},
    {&zwlr_screencopy_manager_v1_interface, 1, NULL},
    {&wp_content_type_manager_v1_interface, 1, NULL},
    {&wp_alpha_modifier_v1_interface, 1, NULL},
    {&zxdg_output_manager_v1_interface, 1, NULL},
    {&wp_fractional_scale_manager_v1_interface, 1, NULL},
    {&zwp_relative_pointer_manager_v1_interface, 1, NULL},
    {&zwp_pointer_gestures_v1_interface, 3, NULL},
    {&zwp_pointer_constraints_v1_interface, 1, NULL},
    {&zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1, NULL},
    {&zwp_idle_inhibit_manager_v1_interface, 1, NULL},
    {&ext_idle_notifier_v1_interface, 1, NULL},
    {&wp_single_pixel_buffer_manager_v1_interface, 1, NULL},
    {&xdg_toplevel_icon_manager_v1_interface, 1, NULL},
    {&wp_presentation_interface, 1, NULL},
    {&xdg_activation_v1_interface, 1, NULL},
    {&zwp_linux_dmabuf_v1_interface, 4, NULL},
    {&wp_cursor_shape_manager_v1_interface, 1, NULL},
    {&wp_color_manager_v1_interface, 1, NULL},
    {&wp_color_representation_manager_v1_interface, 1, NULL},
};

#define NGLOBALS (sizeof(globals) / sizeof(globals[0]))

// in bind mode only this one global is bound, through a stand-in interface
// of the same name: the bind is refused before any event could need the
// real one
static const char* bind_only;
static struct wl_interface stand_in;
static void* bound;

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data;

    if (bind_only) {
        if (!bound && !strcmp(interface, bind_only)) {
            stand_in.name = bind_only;
            stand_in.version = 1;
            bound = wl_registry_bind(registry, name, &stand_in, 1);
        }

        return;
    }

    for (size_t i = 0; i < NGLOBALS; i++) {
        if (globals[i].proxy || strcmp(interface, globals[i].iface->name)) {
            continue;
        }

        uint32_t v = globals[i].version < version ? globals[i].version : version;

        globals[i].proxy = wl_registry_bind(registry, name, globals[i].iface, v);
    }
}

static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void* global(const struct wl_interface* iface) {
    for (size_t i = 0; i < NGLOBALS; i++) {
        if (globals[i].iface == iface) {
            if (!globals[i].proxy) {
                fprintf(stderr, "no %s global\n", iface->name);
                exit(2);
            }

            return globals[i].proxy;
        }
    }

    exit(2);
}

#define G(name) ((struct name*)global(&name##_interface))

static struct wl_display* display;

static struct wl_surface* surface(void) {
    return wl_compositor_create_surface(G(wl_compositor));
}

static struct wl_pointer* pointer(void) {
    return wl_seat_get_pointer(G(wl_seat));
}

static struct ext_image_capture_source_v1* output_source(void) {
    return ext_output_image_capture_source_manager_v1_create_source(
        G(ext_output_image_capture_source_manager_v1), G(wl_output));
}

static struct ext_image_copy_capture_session_v1* output_session(void) {
    return ext_image_copy_capture_manager_v1_create_session(G(ext_image_copy_capture_manager_v1),
                                                            output_source(), 0);
}

static void security_context(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr;
    int pair[2];

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "imway-fault-late-%d", getpid());

    socklen_t len = (socklen_t)(sizeof(addr.sun_family) + 1 + strlen(addr.sun_path + 1));

    if (fd < 0 || bind(fd, (struct sockaddr*)&addr, len) < 0 || listen(fd, 1) < 0 ||
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) < 0) {
        exit(2);
    }

    wp_security_context_manager_v1_create_listener(G(wp_security_context_manager_v1), fd, pair[0]);
}

static void capture_source(void) {
    output_source();
}

static void capture_session(void) {
    output_session();
}

static void capture_frame(void) {
    ext_image_copy_capture_session_v1_create_frame(output_session());
}

static void capture_cursor(void) {
    ext_image_copy_capture_manager_v1_create_pointer_cursor_session(
        G(ext_image_copy_capture_manager_v1), output_source(), pointer());
}

static void screencopy_frame(void) {
    zwlr_screencopy_manager_v1_capture_output(G(zwlr_screencopy_manager_v1), 0, G(wl_output));
}

static void content_type(void) {
    wp_content_type_manager_v1_get_surface_content_type(G(wp_content_type_manager_v1), surface());
}

static void alpha_modifier(void) {
    wp_alpha_modifier_v1_get_surface(G(wp_alpha_modifier_v1), surface());
}

static void xdg_output(void) {
    zxdg_output_manager_v1_get_xdg_output(G(zxdg_output_manager_v1), G(wl_output));
}

static void fractional_scale(void) {
    wp_fractional_scale_manager_v1_get_fractional_scale(G(wp_fractional_scale_manager_v1), surface());
}

static void relative_pointer(void) {
    zwp_relative_pointer_manager_v1_get_relative_pointer(G(zwp_relative_pointer_manager_v1), pointer());
}

static void swipe(void) {
    zwp_pointer_gestures_v1_get_swipe_gesture(G(zwp_pointer_gestures_v1), pointer());
}

static void pinch(void) {
    zwp_pointer_gestures_v1_get_pinch_gesture(G(zwp_pointer_gestures_v1), pointer());
}

static void hold(void) {
    zwp_pointer_gestures_v1_get_hold_gesture(G(zwp_pointer_gestures_v1), pointer());
}

static void locked_pointer(void) {
    zwp_pointer_constraints_v1_lock_pointer(G(zwp_pointer_constraints_v1), surface(), pointer(), NULL,
                                            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
}

static void confined_pointer(void) {
    zwp_pointer_constraints_v1_confine_pointer(G(zwp_pointer_constraints_v1), surface(), pointer(), NULL,
                                               ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
}

static void shortcuts_inhibitor(void) {
    zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
        G(zwp_keyboard_shortcuts_inhibit_manager_v1), surface(), G(wl_seat));
}

static void idle_inhibitor(void) {
    zwp_idle_inhibit_manager_v1_create_inhibitor(G(zwp_idle_inhibit_manager_v1), surface());
}

static void idle_notification(void) {
    ext_idle_notifier_v1_get_idle_notification(G(ext_idle_notifier_v1), 1000, G(wl_seat));
}

static void single_pixel_buffer(void) {
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(G(wp_single_pixel_buffer_manager_v1),
                                                             0, 0, 0, ~0u);
}

static void toplevel_icon(void) {
    xdg_toplevel_icon_manager_v1_create_icon(G(xdg_toplevel_icon_manager_v1));
}

static void presentation_feedback(void) {
    wp_presentation_feedback(G(wp_presentation), surface());
}

static void activation_token(void) {
    xdg_activation_v1_get_activation_token(G(xdg_activation_v1));
}

static void dmabuf_params(void) {
    zwp_linux_dmabuf_v1_create_params(G(zwp_linux_dmabuf_v1));
}

static void dmabuf_default_feedback(void) {
    zwp_linux_dmabuf_v1_get_default_feedback(G(zwp_linux_dmabuf_v1));
}

static void dmabuf_surface_feedback(void) {
    zwp_linux_dmabuf_v1_get_surface_feedback(G(zwp_linux_dmabuf_v1), surface());
}

static void cursor_shape_device(void) {
    wp_cursor_shape_manager_v1_get_pointer(G(wp_cursor_shape_manager_v1), pointer());
}

static void seat_pointer(void) {
    pointer();
}

static void seat_keyboard(void) {
    wl_seat_get_keyboard(G(wl_seat));
}

static void seat_touch(void) {
    wl_seat_get_touch(G(wl_seat));
}

static void colour_output(void) {
    wp_color_manager_v1_get_output(G(wp_color_manager_v1), G(wl_output));
}

static void colour_surface(void) {
    wp_color_manager_v1_get_surface(G(wp_color_manager_v1), surface());
}

static void colour_feedback(void) {
    wp_color_manager_v1_get_surface_feedback(G(wp_color_manager_v1), surface());
}

static void colour_params(void) {
    wp_color_manager_v1_create_parametric_creator(G(wp_color_manager_v1));
}

static void colour_icc(void) {
    wp_color_manager_v1_create_icc_creator(G(wp_color_manager_v1));
}

// the output's description, made through cmMakeImageDesc
static struct wp_image_description_v1* output_description(void) {
    struct wp_color_management_output_v1* out =
        wp_color_manager_v1_get_output(G(wp_color_manager_v1), G(wl_output));

    return wp_color_management_output_v1_get_image_description(out);
}

static void colour_description(void) {
    output_description();
}

static void colour_info(void) {
    struct wp_image_description_v1* desc = output_description();

    // the description is ready once the compositor has answered
    wl_display_roundtrip(display);
    wp_image_description_v1_get_information(desc);
}

static void representation_surface(void) {
    wp_color_representation_manager_v1_get_surface(G(wp_color_representation_manager_v1), surface());
}

static const struct {
    const char* mode;
    void (*chain)(void);
} modes[] = {
    {"security-context", security_context},
    {"capture-source", capture_source},
    {"capture-session", capture_session},
    {"capture-frame", capture_frame},
    {"capture-cursor", capture_cursor},
    {"screencopy-frame", screencopy_frame},
    {"content-type", content_type},
    {"alpha-modifier", alpha_modifier},
    {"xdg-output", xdg_output},
    {"fractional-scale", fractional_scale},
    {"relative-pointer", relative_pointer},
    {"swipe", swipe},
    {"pinch", pinch},
    {"hold", hold},
    {"locked-pointer", locked_pointer},
    {"confined-pointer", confined_pointer},
    {"shortcuts-inhibitor", shortcuts_inhibitor},
    {"idle-inhibitor", idle_inhibitor},
    {"idle-notification", idle_notification},
    {"single-pixel-buffer", single_pixel_buffer},
    {"toplevel-icon", toplevel_icon},
    {"presentation-feedback", presentation_feedback},
    {"activation-token", activation_token},
    {"dmabuf-params", dmabuf_params},
    {"dmabuf-default-feedback", dmabuf_default_feedback},
    {"dmabuf-surface-feedback", dmabuf_surface_feedback},
    {"cursor-shape-device", cursor_shape_device},
    {"seat-pointer", seat_pointer},
    {"seat-keyboard", seat_keyboard},
    {"seat-touch", seat_touch},
    {"colour-output", colour_output},
    {"colour-surface", colour_surface},
    {"colour-feedback", colour_feedback},
    {"colour-params", colour_params},
    {"colour-icc", colour_icc},
    {"colour-description", colour_description},
    {"colour-info", colour_info},
    {"representation-surface", representation_surface},
};

static int expect_no_memory(const char* what) {
    if (wl_display_roundtrip(display) >= 0) {
        fprintf(stderr, "%s: the request went through\n", what);
        return 1;
    }

    const struct wl_interface* iface = NULL;
    uint32_t id = 0;
    uint32_t code = wl_display_get_protocol_error(display, &iface, &id);
    int err = wl_display_get_error(display);

    // libwayland reports the display's no_memory as ENOMEM, not EPROTO
    if (err != ENOMEM && !(err == EPROTO && iface && !strcmp(iface->name, "wl_display") &&
                           code == WL_DISPLAY_ERROR_NO_MEMORY)) {
        fprintf(stderr, "%s: wrong error: errno=%d iface=%s code=%u\n", what, err,
                iface ? iface->name : "?", code);
        return 1;
    }

    printf("%s: no_memory\n", what);

    return 0;
}

int main(int argc, char** argv) {
    alarm(10);
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 2) {
        return 2;
    }

    if (!strcmp(argv[1], "bind")) {
        if (argc < 3) {
            return 2;
        }

        bind_only = argv[2];
    }

    display = wl_display_connect(NULL);

    if (!display) {
        return 2;
    }

    struct wl_registry* registry = wl_display_get_registry(display);

    wl_registry_add_listener(registry, &registry_listener, NULL);

    if (bind_only) {
        wl_display_roundtrip(display);

        if (!bound) {
            fprintf(stderr, "no %s global\n", bind_only);
            return 2;
        }

        return expect_no_memory(bind_only);
    }

    if (wl_display_roundtrip(display) < 0) {
        return 2;
    }

    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (!strcmp(argv[1], modes[i].mode)) {
            modes[i].chain();

            return expect_no_memory(modes[i].mode);
        }
    }

    return 2;
}
