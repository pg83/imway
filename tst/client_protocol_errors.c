#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

static struct wl_compositor* compositor;
static struct wl_subcompositor* subcompositor;
static struct xdg_wm_base* wm_base;
static struct wl_seat* seat;
static struct wl_data_device_manager* data_manager;

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data;
    (void)version;

    if (!strcmp(interface, wl_compositor_interface.name))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, wl_subcompositor_interface.name))
        subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(interface, wl_seat_interface.name))
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
    else if (!strcmp(interface, wl_data_device_manager_interface.name))
        data_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3);
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

    if (wl_display_get_error(display) != EPROTO || !interface ||
        strcmp(interface->name, interface_name) || code != expected) {
        fprintf(stderr, "unexpected protocol error: iface=%s id=%u code=%u errno=%d\n",
                interface ? interface->name : "(none)", object_id, code,
                wl_display_get_error(display));
        return 1;
    }

    return 0;
}

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
        !seat || !data_manager) {
        return 2;
    }

    struct wl_surface* surface = wl_compositor_create_surface(compositor);

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
