// Green toplevel plus a client cursor surface of 1px red/blue stripes. The
// composited cursor fallback draws the surface at the raw pointer position;
// the scenario parks the pointer on a half-pixel and asserts the stripes do
// not bilinearly blend.

#include "wl_util.h"

static struct wl_surface* cursor_surface;
static int cursor_set;

static struct wl_buffer* stripes(int w, int h) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("cursor-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("memfd");
        exit(1);
    }
    uint32_t* px = (uint32_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[y * w + x] = (y & 1) ? 0xFF0000FFu : 0xFFFF0000u;
    munmap(px, size);
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf =
        wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "cursor-snap", 400, 300, 0xFF00FF00u);
    printf("client_reg_cursor_snap: mapped\n");

    cursor_surface = wl_compositor_create_surface(wl_comp);

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (wlp_enter_count && !cursor_set && wl_ptr) {
            wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor_surface, 0, 0);
            wl_surface_attach(cursor_surface, stripes(32, 32), 0, 0);
            wl_surface_damage(cursor_surface, 0, 0, 32, 32);
            wl_surface_commit(cursor_surface);
            wl_display_flush(wl_dpy);
            cursor_set = 1;
            printf("client_reg_cursor_snap: cursor-set\n");
        }
    }
    return 0;
}
