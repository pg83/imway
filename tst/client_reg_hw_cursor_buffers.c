// A green 400x300 toplevel whose cursor surface shows, one KEY_A press at a
// time, the buffers a hardware cursor plane has to handle, printing each:
//   "cursor spb"   a single-pixel magenta buffer (set on the first enter)
//   "cursor xrgb"  a 16x16 XRGB8888 cyan buffer whose top byte is zero
//   "cursor big"   a 96x96 ARGB8888 cyan buffer, larger than the plane

#include "wl_util.h"

#include <single-pixel-buffer-v1-client-protocol.h>

static struct wp_single_pixel_buffer_manager_v1* sp_mgr;

static void extra_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    (void)v;
    if (!strcmp(iface, wp_single_pixel_buffer_manager_v1_interface.name))
        sp_mgr = wl_registry_bind(r, name, &wp_single_pixel_buffer_manager_v1_interface, 1);
}
static void extra_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}
static const struct wl_registry_listener extra_listener = {extra_global, extra_remove};

static struct wl_buffer* shm_buffer(int w, int h, uint32_t pixel, uint32_t format) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("cursor-buffer", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) px[i] = pixel;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

static void show(struct wl_surface* cursor, struct wl_buffer* b, int w, int h, const char* what) {
    wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor, 0, 0);
    wl_surface_attach(cursor, b, 0, 0);
    wl_surface_damage(cursor, 0, 0, w, h);
    wl_surface_commit(cursor);
    wl_display_flush(wl_dpy);
    printf("%s\n", what);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;
    struct wl_registry* reg = wl_display_get_registry(wl_dpy);
    wl_registry_add_listener(reg, &extra_listener, NULL);
    wl_display_roundtrip(wl_dpy);
    if (!sp_mgr) {
        fprintf(stderr, "no single-pixel buffers\n");
        return 1;
    }

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "hw-cursor-buffers", 400, 300, 0xff00ff00);
    printf("mapped\n");

    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    int phase = -1;

    wlk_watch_key = 30; // KEY_A

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (phase < 0 && wlp_enter_count && wl_ptr) {
            phase = 0;
            show(cursor, wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(sp_mgr, 0xffffffff, 0, 0xffffffff, 0xffffffff), 1, 1, "cursor spb");
        }
        while (phase >= 0 && wlk_watch_hits >= 2 * (phase + 1) && phase < 2) {
            phase++;
            if (phase == 1) {
                show(cursor, shm_buffer(16, 16, 0x0000ffffu, WL_SHM_FORMAT_XRGB8888), 16, 16, "cursor xrgb");
            } else {
                show(cursor, shm_buffer(96, 96, 0xff00ffffu, WL_SHM_FORMAT_ARGB8888), 96, 96, "cursor big");
            }
        }
    }
    return 0;
}
