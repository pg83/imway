// Feature: partial damage over ping-ponging shm buffers. Two 200x200
// buffers alternate; each commit damages only the union of what changed
// since the previous commit (double-buffered clients must include the
// previous frame's delta). A partial-copy bug in the compositor shows as a
// stale or misplaced square. Phases stepped by the scenario via keys:
//   KEY_1: buffer B = red + green square at (150,20) 40x40, damage that rect
//   KEY_2: buffer A = red + blue square at (20,150) 40x40, damage both rects

#include "wl_util.h"
#include <linux/input-event-codes.h>

static struct wl_toplevel_ctx top;

struct membuf {
    struct wl_buffer* buf;
    uint32_t* px;
};

static struct membuf make_membuf(int w, int h, uint32_t argb) {
    int stride = w * 4, size = stride * h;
    int fd = memfd_create("damage-shm", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { perror("memfd"); exit(1); }
    uint32_t* px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < w * h; i++) px[i] = argb;
    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return (struct membuf){buf, px};
}

static void fill_rect(struct membuf* b, int x0, int y0, int w, int h, uint32_t argb) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) b->px[y * 200 + x] = argb;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    if (wl_boot()) return 1;

    wl_make_toplevel(&top, "damage", 200, 200, 0xFFFF0000);
    struct membuf a = make_membuf(200, 200, 0xFFFF0000);
    struct membuf b = make_membuf(200, 200, 0xFFFF0000);

    // state1: buffer a, full red, full damage
    wl_surface_attach(top.surface, a.buf, 0, 0);
    wl_surface_damage(top.surface, 0, 0, 200, 200);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("state1\n");

    wlk_watch_key = KEY_1;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    // state2: buffer b gains a green square, damage only that rect
    fill_rect(&b, 150, 20, 40, 40, 0xFF00FF00);
    wl_surface_attach(top.surface, b.buf, 0, 0);
    wl_surface_damage(top.surface, 150, 20, 40, 40);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("state2\n");

    wlk_watch_key = KEY_2;
    wlk_watch_hits = 0;
    while (!wlk_watch_hits && wl_display_dispatch(wl_dpy) != -1) {
    }

    // state3: back to buffer a, now with a blue square; damage must cover
    // the green square (gone vs the last commit) and the new blue one
    fill_rect(&a, 20, 150, 40, 40, 0xFF0000FF);
    wl_surface_attach(top.surface, a.buf, 0, 0);
    wl_surface_damage(top.surface, 150, 20, 40, 40);
    wl_surface_damage(top.surface, 20, 150, 40, 40);
    wl_surface_commit(top.surface);
    wl_display_roundtrip(wl_dpy);
    printf("state3\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }
    return 0;
}
