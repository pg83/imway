#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
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

static struct wl_compositor* compositor;
static struct wl_subcompositor* subcompositor;
static struct xdg_wm_base* wm_base;
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

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    (void)data;
    (void)version;

    if (!strcmp(interface, wl_compositor_interface.name))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 6);
    else if (!strcmp(interface, wl_subcompositor_interface.name))
        subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
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
        !content_type || !alpha_mod || !representation || !dmabuf) {
        return 2;
    }

    struct wl_surface* surface = wl_compositor_create_surface(compositor);

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
