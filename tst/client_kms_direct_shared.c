/* The fullscreen red direct-scanout candidate of the scanout taint client,
 * with two wl_buffers made from the one dumb buffer object (the same prime
 * fd). It maps with the first, then moves one step per KEY_A press,
 * printing each:
 *   "second"       the second wl_buffer attached
 *   "first again"  the first attached again
 *   "second gone"  the second wl_buffer destroyed
 *   "surface gone" the toplevel and the first wl_buffer destroyed */
#define main taint_main
#include "client_kms_scanout_taint.c"
#undef main

static struct wl_buffer* shared_buffer(int prime, uint32_t pitch) {
    struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
    zwp_linux_buffer_params_v1_add(params, prime, 0, 0, pitch, 0, 0);
    struct wl_buffer* b = zwp_linux_buffer_params_v1_create_immed(params, W, H, FOURCC_XRGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    return b;
}

// one red dumb buffer object, exported once; -1 when no card node has one
static int red_prime(uint32_t* pitch) {
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        struct drm_mode_create_dumb create = {0};
        create.width = W;
        create.height = H;
        create.bpp = 32;
        struct drm_mode_map_dumb map = {0};
        int prime = -1;
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
            close(fd);
            continue;
        }
        map.handle = create.handle;
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0 ||
            drmPrimeHandleToFD(fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &prime) != 0 || prime < 0) {
            close(fd);
            continue;
        }

        uint32_t* px = mmap(NULL, (size_t)create.pitch * H, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
        if (px != MAP_FAILED) {
            for (size_t j = 0; j < (size_t)create.pitch * H / 4; j++) px[j] = 0xffff2020u;
            munmap(px, (size_t)create.pitch * H);
        }
        close(fd); /* the prime fd keeps the buffer alive */
        *pitch = create.pitch;
        return prime;
    }
    return -1;
}

static void step(const char* what) {
    wl_surface_commit(surface);
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
    if (!dmabuf) return 77;
    wl_display_roundtrip(wl_dpy);
    if (!linear_ok) return 77;

    uint32_t pitch = 0;
    int prime = red_prime(&pitch);
    if (prime < 0) return 77;

    struct wl_buffer* second = shared_buffer(prime, pitch);

    buffer = shared_buffer(prime, pitch);
    close(prime);

    surface = wl_compositor_create_surface(wl_comp);
    xs = xdg_wm_base_get_xdg_surface(wl_wm, surface);
    xdg_surface_add_listener(xs, &xs_listener, NULL);
    tl = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(tl, &tl_listener, NULL);
    xdg_toplevel_set_title(tl, "kms-taint");
    xdg_toplevel_set_app_id(tl, "kms-taint");
    xdg_toplevel_set_fullscreen(tl, NULL);
    wl_surface_commit(surface);

    int phase = 0;

    wlk_watch_key = 30; // KEY_A

    while (wl_display_dispatch(wl_dpy) >= 0) {
        while (wlk_watch_hits >= 2 * (phase + 1) && phase < 4) {
            phase++;
            switch (phase) {
                case 1:
                    wl_surface_attach(surface, second, 0, 0);
                    wl_surface_damage(surface, 0, 0, W, H);
                    step("second");
                    break;
                case 2:
                    wl_surface_attach(surface, buffer, 0, 0);
                    wl_surface_damage(surface, 0, 0, W, H);
                    step("first again");
                    break;
                case 3:
                    wl_buffer_destroy(second);
                    wl_display_flush(wl_dpy);
                    printf("second gone\n");
                    break;
                case 4:
                    xdg_toplevel_destroy(tl);
                    xdg_surface_destroy(xs);
                    wl_surface_destroy(surface);
                    wl_buffer_destroy(buffer);
                    wl_display_flush(wl_dpy);
                    printf("surface gone\n");
                    break;
            }
        }
    }

    return 0;
}
