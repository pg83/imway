// Green toplevel whose pointer cursor is its own 32x32 surface of red/blue
// stripes, set on the first enter ("cursor-set"). A press of KEY_A hides the
// cursor with a null surface ("cursor-hidden").

#include "wl_util.h"

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
    alarm(60);
    if (wl_boot()) return 1;

    struct wl_toplevel_ctx ctx;

    wl_make_toplevel(&ctx, "kms-cursor", 400, 300, 0xFF00FF00u);
    printf("client_kms_cursor_surface: mapped\n");

    struct wl_surface* cursor = wl_compositor_create_surface(wl_comp);
    int set = 0, hidden = 0;

    wlk_watch_key = 30; // KEY_A

    while (wl_display_dispatch(wl_dpy) != -1) {
        if (wlp_enter_count && !set && wl_ptr) {
            wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, cursor, 0, 0);
            wl_surface_attach(cursor, stripes(32, 32), 0, 0);
            wl_surface_damage(cursor, 0, 0, 32, 32);
            wl_surface_commit(cursor);
            wl_display_flush(wl_dpy);
            set = 1;
            printf("client_kms_cursor_surface: cursor-set\n");
        }
        if (set && !hidden && wlk_watch_hits) {
            wl_pointer_set_cursor(wl_ptr, wlp_enter_serial, NULL, 0, 0);
            wl_display_flush(wl_dpy);
            hidden = 1;
            printf("client_kms_cursor_surface: cursor-hidden\n");
        }
    }
    return 0;
}
