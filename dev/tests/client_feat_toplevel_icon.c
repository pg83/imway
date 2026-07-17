// Feature: xdg-toplevel-icon. Attaching a client-drawn icon buffer must be
// accepted, and a malformed (undersized-stride) icon buffer must be ignored
// rather than crashing the compositor (the iconAddBuffer stride guard).

#include "wl_util.h"
#include <xdg-toplevel-icon-v1-client-protocol.h>

static struct xdg_toplevel_icon_manager_v1* icon_mgr;
static struct wl_toplevel_ctx top;
static int icon_size;

static void mgr_icon_size(void* d, struct xdg_toplevel_icon_manager_v1* m, int32_t s) {
    (void)d; (void)m; icon_size = s;
}
static void mgr_done(void* d, struct xdg_toplevel_icon_manager_v1* m) { (void)d; (void)m; }
static const struct xdg_toplevel_icon_manager_v1_listener mgr_listener = {mgr_icon_size, mgr_done};

static void reg2_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d; (void)v;
    if (!strcmp(iface, xdg_toplevel_icon_manager_v1_interface.name))
        icon_mgr = wl_registry_bind(r, name, &xdg_toplevel_icon_manager_v1_interface, 1);
}
static void reg2_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg2_listener = {reg2_global, reg2_remove};

// a solid shm buffer with an explicit (possibly bogus) stride
static struct wl_buffer* icon_buffer(int w, int h, int stride_bytes, uint32_t argb) {
    int size = stride_bytes * h;
    if (size < w * 4) size = w * h * 4; // keep the pool large enough to map
    int fd = memfd_create("icon", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { perror("memfd"); exit(1); }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) px[i] = argb;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* b = wl_shm_pool_create_buffer(pool, 0, w, h, stride_bytes, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return b;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_registry* reg2 = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg2, &reg2_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!icon_mgr) { fprintf(stderr, "no toplevel-icon manager\n"); return 1; }
    xdg_toplevel_icon_manager_v1_add_listener(icon_mgr, &mgr_listener, NULL);
    wl_display_roundtrip(wl_dpy);

    wl_make_toplevel(&top, "client_feat_toplevel_icon", 300, 200, 0xFFFF0000);

    int sz = icon_size > 0 ? icon_size : 48;

    // a well-formed icon
    struct xdg_toplevel_icon_v1* good = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);
    xdg_toplevel_icon_v1_add_buffer(good, icon_buffer(sz, sz, sz * 4, 0xFF00FF00), 1);
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, top.tl, good);
    wl_display_roundtrip(wl_dpy);

    // a malformed icon: stride far smaller than width*4 — must be ignored, not
    // walked past the mmap
    struct xdg_toplevel_icon_v1* bad = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);
    xdg_toplevel_icon_v1_add_buffer(bad, icon_buffer(sz, sz, sz, 0xFF0000FF), 1);
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, top.tl, bad);

    // if either request errored, the next roundtrip returns < 0
    if (wl_display_roundtrip(wl_dpy) < 0) { fprintf(stderr, "protocol error on icon\n"); return 1; }
    if (wl_display_roundtrip(wl_dpy) < 0) { fprintf(stderr, "protocol error on icon\n"); return 1; }

    printf("client_feat_toplevel_icon: icon accepted (size hint %d), bad stride ignored\n", icon_size);
    printf("client_feat_toplevel_icon: ok\n");
    return 0;
}
