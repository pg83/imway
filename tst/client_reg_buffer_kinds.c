// One 200x150 toplevel surface that changes the kind of buffer it shows,
// one step per KEY_A press, printing "step N" once each is committed:
//   0 wl_shm ARGB8888 red        3 wl_shm ARGB8888 blue
//   1 dma-buf (dumb) orange      4 single-pixel yellow, viewport to 200x150
//   2 wl_shm XRGB8888 green      5 wl_shm ARGB8888 red, viewport unset
// Exits 77 without dumb-buffer dma-bufs.

#include "wl_util.h"
#include "dumb_dmabuf.inc"

#include <single-pixel-buffer-v1-client-protocol.h>
#include <viewporter-client-protocol.h>

static struct wp_single_pixel_buffer_manager_v1* sp_mgr;
static struct wp_viewporter* viewporter;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, wp_single_pixel_buffer_manager_v1_interface.name))
        sp_mgr = wl_registry_bind(r, name, &wp_single_pixel_buffer_manager_v1_interface, 1);
    else if (!strcmp(iface, wp_viewporter_interface.name))
        viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_buffer* shm_solid(int w, int h, uint32_t xrgb, uint32_t format) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("buffer-kinds", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) {
        // an XRGB buffer's top byte is undefined: leave it 0, the compositor
        // must not read it as alpha
        px[i] = format == WL_SHM_FORMAT_XRGB8888 ? (xrgb & 0xffffff) : (0xff000000u | xrgb);
    }
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static void show(struct wl_surface* s, struct wl_buffer* b, int step) {
    wl_surface_attach(s, b, 0, 0);
    wl_surface_damage(s, 0, 0, 200, 150);
    wl_surface_commit(s);
    wl_display_flush(wl_dpy);
    printf("step %d\n", step);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;
    int rc = dumb_boot();
    if (rc) return rc;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!sp_mgr || !viewporter) {
        fprintf(stderr, "no single-pixel buffers or viewporter\n");
        return 1;
    }
    struct wl_buffer* orange = dumb_buffer(200, 150, 0xffff8000);
    if (!orange) return 77;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "buffer-kinds", 200, 150, 0xffff0000);
    struct wp_viewport* vp = wp_viewporter_get_viewport(viewporter, top.surface);
    printf("step 0\n");

    wlk_watch_key = 30; // KEY_A
    int step = 0;

    while (wl_display_dispatch(wl_dpy) != -1) {
        while (wlk_watch_hits >= 2 * (step + 1) && step < 5) {
            step++;
            switch (step) {
                case 1:
                    show(top.surface, orange, 1);
                    break;
                case 2:
                    show(top.surface, shm_solid(200, 150, 0x00ff00, WL_SHM_FORMAT_XRGB8888), 2);
                    break;
                case 3:
                    show(top.surface, shm_solid(200, 150, 0x0000ff, WL_SHM_FORMAT_ARGB8888), 3);
                    break;
                case 4:
                    wp_viewport_set_destination(vp, 200, 150);
                    show(top.surface, wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(sp_mgr, 0xffffffff, 0xffffffff, 0, 0xffffffff), 4);
                    break;
                case 5:
                    wp_viewport_set_destination(vp, -1, -1);
                    show(top.surface, shm_solid(200, 150, 0xff0000, WL_SHM_FORMAT_ARGB8888), 5);
                    break;
            }
        }
    }
    return 0;
}
