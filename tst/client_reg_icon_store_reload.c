// Regression: a named xdg-toplevel-icon must survive an icon store reload.
// The name resolves to a store-owned Icon from the current generation; a
// reload drops that generation, so the compositor has to re-resolve the name
// instead of keeping a pointer into the dead arena.

#include "wl_util.h"

#include <xdg-toplevel-icon-v1-client-protocol.h>

static struct xdg_toplevel_icon_manager_v1* icon_mgr;

static void mgr_icon_size(void* d, struct xdg_toplevel_icon_manager_v1* manager, int32_t size) {
    (void)d; (void)manager; (void)size;
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

    struct wl_toplevel_ctx c;
    wl_make_toplevel(&c, "iconreload", 300, 200, 0xFF2040A0);
    wl_display_roundtrip(wl_dpy);

    // named icon: the scenario staged imway-test-icon.svg into XDG_DATA_HOME
    struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);
    xdg_toplevel_icon_v1_set_name(icon, "imway-test-icon");
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, c.tl, icon);
    xdg_toplevel_icon_v1_destroy(icon);
    wl_surface_commit(c.surface);
    wl_display_flush(wl_dpy);
    printf("client_reg_icon_store_reload: icon committed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
