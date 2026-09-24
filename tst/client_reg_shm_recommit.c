// One sealed wl_shm buffer committed twice: green, then, once the
// compositor released it, repainted blue in place and attached again. With
// the argument "outside" the second commit's damage lies wholly outside the
// buffer. With "shared" the green buffer goes to two windows at once and
// comes back released once both have taken it. With "shared-moveon" the
// compositor samples it in place, so it stays in use while it shows, and
// comes back once both windows have moved on to buffers of their own.

#include "wl_util.h"

#include <fcntl.h>

static int released;

static void buffer_release(void* data, struct wl_buffer* buffer) {
    (void)data;
    (void)buffer;
    released = 1;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void fill(uint32_t* pixels, int count, uint32_t argb) {
    for (int i = 0; i < count; i++) {
        pixels[i] = argb;
    }
}

static void commit(struct wl_surface* surface, struct wl_buffer* buffer, int w, int h, int outside) {
    wl_surface_attach(surface, buffer, 0, 0);

    if (outside) {
        wl_surface_damage_buffer(surface, w + 100, h + 100, 10, 10);
    } else {
        wl_surface_damage_buffer(surface, 0, 0, w, h);
    }

    wl_surface_commit(surface);
    wl_display_flush(wl_dpy);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    if (wl_boot()) {
        return 1;
    }

    int outside = argc > 1 && !strcmp(argv[1], "outside");
    int moveon = argc > 1 && !strcmp(argv[1], "shared-moveon");
    int shared = moveon || (argc > 1 && !strcmp(argv[1], "shared"));
    int w = 640;
    int h = 480;
    int stride = w * 4;
    int size = stride * h;
    int fd = memfd_create("recommit-shm", MFD_ALLOW_SEALING);

    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("sealed memfd");
        return 1;
    }

    uint32_t* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (pixels == MAP_FAILED) {
        perror("sealed mmap");
        return 1;
    }

    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) < 0) {
        perror("sealed fcntl");
        return 1;
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(wl_shm_g, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(buffer, &buffer_listener, NULL);

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "shm-recommit", w, h, 0xff00ff00u);
    fill(pixels, w * h, 0xff00ff00u);

    if (shared) {
        struct wl_toplevel_ctx two;

        wl_make_toplevel(&two, "shm-recommit-two", w, h, 0xff0000ffu);
        commit(top.surface, buffer, w, h, 0);
        commit(two.surface, buffer, w, h, 0);
        wl_await_presented(top.surface);
        wl_await_presented(two.surface);
        printf("green shared\n");

        if (moveon) {
            wl_display_roundtrip(wl_dpy);

            if (released) {
                fprintf(stderr, "the buffer came back while both windows showed it\n");
                return 1;
            }

            commit(top.surface, wl_solid(w, h, 0xff0000ffu), w, h, 0);
            commit(two.surface, wl_solid(w, h, 0xff0000ffu), w, h, 0);
            printf("both moved on\n");
        }

        // both copies taken, or both windows moved on: the buffer is the
        // client's again
        while (!released && wl_display_dispatch(wl_dpy) != -1) {
        }

        printf("shared buffer released\n");

        while (wl_display_dispatch(wl_dpy) != -1) {
        }

        return 0;
    }

    commit(top.surface, buffer, w, h, 0);
    printf("green committed\n");

    while (!released && wl_display_dispatch(wl_dpy) != -1) {
    }

    if (!released) {
        return 1;
    }

    released = 0;
    fill(pixels, w * h, 0xff0000ffu);
    commit(top.surface, buffer, w, h, outside);
    printf("blue committed\n");

    while (wl_display_dispatch(wl_dpy) != -1) {
    }

    wl_buffer_destroy(buffer);
    munmap(pixels, size);

    return 0;
}
