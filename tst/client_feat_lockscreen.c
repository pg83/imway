// A live surface behind the lockscreen. It alternates two high-contrast
// checkerboards so the scenario can prove the filtered background keeps
// updating, while KEY_F8 verifies that client keyboard input is withheld.

#include "wl_util.h"
#include <linux/input-event-codes.h>

struct membuf {
    struct wl_buffer* buffer;
    uint32_t* pixels;
};

static struct membuf make_buffer(int w, int h, int inverted) {
    int stride = w * 4;
    int size = stride * h;
    int fd = memfd_create("lock-live", 0);

    if (fd < 0 || ftruncate(fd, size) < 0) exit(1);

    uint32_t* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (pixels == MAP_FAILED) exit(1);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int cell = ((x / 24) ^ (y / 24) ^ inverted) & 1;

            pixels[y * w + x] = inverted
                ? (cell ? 0xFF20FF50 : 0xFFA020FF)
                : (cell ? 0xFFFF2020 : 0xFF20B8FF);
        }
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return (struct membuf){buffer, pixels};
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);

    if (wl_boot() || !wl_kbd) return 1;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "lock-live", 720, 480, 0xFFFF2020);
    struct membuf buffers[2] = {
        make_buffer(720, 480, 0),
        make_buffer(720, 480, 1),
    };

    wlk_watch_key = KEY_F8;
    printf("lockscreen ready\n");

    int phase = 0;

    while (wlk_watch_hits < 2) {
        struct membuf* frame = &buffers[phase & 1];

        wl_surface_attach(top.surface, frame->buffer, 0, 0);
        wl_surface_damage(top.surface, 0, 0, 720, 480);
        wl_surface_commit(top.surface);
        wl_display_roundtrip(wl_dpy);
        printf("phase %d\n", ++phase);

        for (int i = 0; i < 20 && wlk_watch_hits < 2; i++) {
            wl_display_roundtrip(wl_dpy);
            usleep(50000);
        }
    }

    printf("lockscreen input restored\n");
    return 0;
}
