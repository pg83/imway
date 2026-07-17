// Feature: xdg-toplevel-icon.  set_icon is double-buffered with wl_surface
// state, and destroying the source icon after set_icon must not lose the
// pending toplevel state.  The scenario observes the green icon in Alt+Tab.

#include "wl_util.h"

#include <xdg-toplevel-icon-v1-client-protocol.h>

static struct xdg_toplevel_icon_manager_v1* icon_mgr;
static int icon_size;

static void mgr_icon_size(void* d, struct xdg_toplevel_icon_manager_v1* manager, int32_t size) {
    (void)d; (void)manager;
    icon_size = size;
}

static void mgr_done(void* d, struct xdg_toplevel_icon_manager_v1* manager) {
    (void)d; (void)manager;
}

static const struct xdg_toplevel_icon_manager_v1_listener mgr_listener = {
    .icon_size = mgr_icon_size,
    .done = mgr_done,
};

static void extra_global(void* d, struct wl_registry* registry, uint32_t name,
                         const char* interface, uint32_t version) {
    (void)d; (void)version;
    if (!strcmp(interface, xdg_toplevel_icon_manager_v1_interface.name))
        icon_mgr = wl_registry_bind(registry, name, &xdg_toplevel_icon_manager_v1_interface, 1);
}

static void extra_remove(void* d, struct wl_registry* registry, uint32_t name) {
    (void)d; (void)registry; (void)name;
}

static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static void wait_marker(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", getenv("XDG_RUNTIME_DIR"), name);
    while (access(path, F_OK) != 0) {
        wl_display_dispatch_pending(wl_dpy);
        wl_display_flush(wl_dpy);
        usleep(20000);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    if (wl_boot()) return 1;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!icon_mgr) return 1;
    xdg_toplevel_icon_manager_v1_add_listener(icon_mgr, &mgr_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    struct wl_toplevel_ctx a, b;
    wl_make_toplevel(&a, "icon-red", 300, 200, 0xFFFF0000);
    wl_make_toplevel(&b, "icon-blue", 300, 200, 0xFF0000FF);

    int size = icon_size > 0 ? icon_size : 48;
    struct wl_buffer* superseded = wl_solid(size, size, 0xFFFF00FF);
    struct wl_buffer* buffer = wl_solid(size, size, 0xFF00FF00);
    struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);
    xdg_toplevel_icon_v1_add_buffer(icon, superseded, 1);
    // Same size+scale replaces the earlier raster; the switcher must use this
    // green image, not the superseded magenta one.
    xdg_toplevel_icon_v1_add_buffer(icon, buffer, 1);
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, a.tl, icon);

    // set_icon snapshots the icon state.  Both source objects may die before
    // the wl_surface commit that applies that snapshot.
    xdg_toplevel_icon_v1_destroy(icon);
    wl_buffer_destroy(superseded);
    wl_buffer_destroy(buffer);
    wl_display_flush(wl_dpy);
    printf("client_feat_toplevel_icon: icon pending\n");

    wait_marker("go-icon-commit");
    wl_surface_commit(a.surface);
    wl_display_flush(wl_dpy);
    printf("client_feat_toplevel_icon: icon committed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
