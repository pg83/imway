// Fatal xdg-toplevel-icon validation cases.  Each mode needs a fresh client
// because the expected protocol error disconnects it.

#include "wl_util.h"

#include <errno.h>
#include <xdg-toplevel-icon-v1-client-protocol.h>

static struct xdg_toplevel_icon_manager_v1* icon_mgr;

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

static struct wl_buffer* shm_buffer(int width, int height, uint32_t color) {
    int stride = width * 4;
    int bytes = stride * height;
    int fd = memfd_create("icon-errors", 0);
    if (fd < 0 || ftruncate(fd, bytes) < 0) return NULL;
    uint32_t* pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) return NULL;
    for (int i = 0; i < width * height; i++) pixels[i] = color;
    munmap(pixels, bytes);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, bytes);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static int expect_error(uint32_t want_code) {
    (void)wl_display_roundtrip(wl_dpy);
    const struct wl_interface* interface = NULL;
    uint32_t object_id = 0;
    uint32_t code = wl_display_get_protocol_error(wl_dpy, &interface, &object_id);
    if (wl_display_get_error(wl_dpy) != EPROTO || !interface ||
        strcmp(interface->name, xdg_toplevel_icon_v1_interface.name) || code != want_code) {
        fprintf(stderr, "wrong/no error: %s code %u, want %s code %u (errno=%d)\n",
                interface ? interface->name : "?", code,
                xdg_toplevel_icon_v1_interface.name, want_code, wl_display_get_error(wl_dpy));
        return 1;
    }
    printf("error ok: %s code %u\n", interface->name, code);
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(10);
    if (argc != 2 || wl_boot()) return 2;
    struct wl_registry* registry = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(registry, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!icon_mgr) return 2;

    struct xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(icon_mgr);

    if (!strcmp(argv[1], "invalid-buffer")) {
        xdg_toplevel_icon_v1_add_buffer(icon, shm_buffer(48, 32, 0xFF00FF00), 1);
        return expect_error(XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER);
    }

    struct wl_buffer* first = shm_buffer(48, 48, 0xFF00FF00);
    xdg_toplevel_icon_v1_add_buffer(icon, first, 1);

    if (!strcmp(argv[1], "no-buffer")) {
        wl_buffer_destroy(first);
        return expect_error(XDG_TOPLEVEL_ICON_V1_ERROR_NO_BUFFER);
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "client_reg_toplevel_icon_errors", 200, 120, 0xFFFF0000);
    xdg_toplevel_icon_manager_v1_set_icon(icon_mgr, top.tl, icon);

    if (!strcmp(argv[1], "immutable-name")) {
        xdg_toplevel_icon_v1_set_name(icon, "changed-after-assignment");
        return expect_error(XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE);
    }

    if (!strcmp(argv[1], "immutable-buffer")) {
        xdg_toplevel_icon_v1_add_buffer(icon, shm_buffer(64, 64, 0xFF0000FF), 1);
        return expect_error(XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE);
    }

    return 2;
}
