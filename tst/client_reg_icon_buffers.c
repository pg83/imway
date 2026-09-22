#include "wl_util.h"

#include <xdg-toplevel-icon-v1-client-protocol.h>

// Which raster an xdg-toplevel-icon keeps. A smaller buffer added after a
// larger one is watched but not taken; an XRGB buffer is accepted like an
// ARGB one; a second set_icon before the commit replaces the first; a
// null icon clears. The scenario reads the result off the dump's icon_w.

static struct xdg_toplevel_icon_manager_v1* icon_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name,
                         const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_toplevel_icon_manager_v1_interface.name))
        icon_mgr = wl_registry_bind(r, name, &xdg_toplevel_icon_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_buffer* square(int edge, uint32_t format) {
    int stride = edge * 4, size = stride * edge;
    int fd = memfd_create("icon-buffers", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        exit(2);
    }

    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    for (int i = 0; i < edge * edge; i++) {
        px[i] = 0x0000ff00u; // alpha byte left zero: only XRGB may render it
    }

    munmap(px, size);

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, edge, edge, stride, format);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buffer;
}

static struct xdg_toplevel_icon_v1* icon_of(int first, int second, uint32_t format) {
    struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);

    xdg_toplevel_icon_v1_add_buffer(icon, square(first, format), 1);
    if (second)
        xdg_toplevel_icon_v1_add_buffer(icon, square(second, format), 1);

    return icon;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(20);

    if (wl_boot()) return 2;

    struct wl_registry* registry = wl_display_get_registry(wl_dpy);

    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    if (!icon_mgr) return 2;

    struct wl_toplevel_ctx big, xrgb, twice, cleared;

    wl_make_toplevel(&big, "icon-keeps-big", 120, 120, 0xff202020u);
    wl_make_toplevel(&xrgb, "icon-xrgb", 120, 120, 0xff202020u);
    wl_make_toplevel(&twice, "icon-twice", 120, 120, 0xff202020u);
    wl_make_toplevel(&cleared, "icon-cleared", 120, 120, 0xff202020u);

    // 64 first, then 32: the smaller one does not replace the larger
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, big.tl, icon_of(64, 32, WL_SHM_FORMAT_ARGB8888));
    wl_surface_commit(big.surface);

    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, xrgb.tl, icon_of(48, 0, WL_SHM_FORMAT_XRGB8888));
    wl_surface_commit(xrgb.surface);

    // the second set_icon before the commit wins
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, twice.tl, icon_of(64, 0, WL_SHM_FORMAT_ARGB8888));
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, twice.tl, icon_of(40, 0, WL_SHM_FORMAT_ARGB8888));
    wl_surface_commit(twice.surface);

    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, cleared.tl, icon_of(64, 0, WL_SHM_FORMAT_ARGB8888));
    wl_surface_commit(cleared.surface);
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, cleared.tl, NULL);
    wl_surface_commit(cleared.surface);

    wl_display_roundtrip(wl_dpy);
    printf("icons set\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    return 0;
}
