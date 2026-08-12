/* PLAN: acquire_fence_parking — a client that keeps committing dmabufs whose
 * acquire points never signal must not drag the compositor's frame loop down:
 * the commit parks until the point materializes instead of stalling a render.
 * A victim window measures the frame-callback rate while the hostile window
 * floods unsignaled acquire points; afterwards the points are signaled and the
 * parked content must resume. */
#include "syncobj_error.inc"

#include <poll.h>
#include <time.h>

/* a 1x1 ARGB dmabuf from a DRM dumb buffer: environments without
 * /dev/udmabuf (the shared helper's source) still have a card node */
static struct wl_buffer* park_dumb_dmabuf(void) {
    if (!sync_dmabuf || !sync_linear_argb) return NULL;
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        struct drm_mode_create_dumb create = {0};
        create.width = 1;
        create.height = 1;
        create.bpp = 32;
        int prime = -1;
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0 ||
            drmPrimeHandleToFD(fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &prime) != 0 ||
            prime < 0) {
            close(fd);
            continue;
        }
        /* the prime fd holds its own dma-buf reference */
        close(fd);
        struct zwp_linux_buffer_params_v1* params =
            zwp_linux_dmabuf_v1_create_params(sync_dmabuf);
        zwp_linux_buffer_params_v1_add(params, prime, 0, 0, create.pitch, 0, 0);
        close(prime);
        return zwp_linux_buffer_params_v1_create_immed(params, 1, 1, 0x34325241u, 0);
    }
    return NULL;
}

static int frames_seen;

static void park_frame_done(void* d, struct wl_callback* cb, uint32_t t) {
    (void)d; (void)t;
    wl_callback_destroy(cb);
    frames_seen++;
}
static const struct wl_callback_listener park_frame_listener = {park_frame_done};

static uint64_t park_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* pump the connection for up to wait_ms, dispatching whatever arrives */
static void park_pump(int wait_ms) {
    wl_display_flush(wl_dpy);
    struct pollfd p = {wl_display_get_fd(wl_dpy), POLLIN, 0};
    poll(&p, 1, wait_ms);
    if (p.revents & POLLIN) {
        wl_display_dispatch(wl_dpy);
    } else {
        wl_display_dispatch_pending(wl_dpy);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    int rc = sync_test_boot();
    if (rc) return rc;

    struct wp_linux_drm_syncobj_timeline_v1* acquire = sync_test_timeline_keep();
    struct wp_linux_drm_syncobj_timeline_v1* release = sync_test_timeline();
    struct wl_buffer* buffer = sync_test_dmabuf();
    if (!buffer) buffer = park_dumb_dmabuf();
    if (!acquire || !release || !buffer) return 77;

    struct wl_toplevel_ctx victim, hostile;
    wl_make_toplevel(&victim, "syncobj-park-victim", 200, 150, 0xff2040c0);
    wl_make_toplevel(&hostile, "syncobj-park-hostile", 200, 150, 0xffc04020);

    struct wp_linux_drm_syncobj_surface_v1* sync_surface =
        wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, hostile.surface);

    /* measure the victim's frame-callback rate while the hostile surface
     * commits a fresh never-signaled acquire point every ~30 ms */
    struct wl_callback* cb = wl_surface_frame(victim.surface);
    wl_callback_add_listener(cb, &park_frame_listener, NULL);
    wl_surface_commit(victim.surface);

    uint64_t start = park_now_ms(), last_hostile = 0;
    uint64_t point = 0;
    int last_seen = 0;

    while (park_now_ms() - start < 1200) {
        uint64_t now = park_now_ms();
        if (now - last_hostile >= 30) {
            point++;
            wp_linux_drm_syncobj_surface_v1_set_acquire_point(
                sync_surface, acquire, (uint32_t)(point >> 32), (uint32_t)point);
            wp_linux_drm_syncobj_surface_v1_set_release_point(
                sync_surface, release, (uint32_t)(point >> 32), (uint32_t)point);
            wl_surface_attach(hostile.surface, buffer, 0, 0);
            wl_surface_damage(hostile.surface, 0, 0, 1, 1);
            wl_surface_commit(hostile.surface);
            last_hostile = now;
        }
        if (frames_seen != last_seen) {
            last_seen = frames_seen;
            cb = wl_surface_frame(victim.surface);
            wl_callback_add_listener(cb, &park_frame_listener, NULL);
            wl_surface_commit(victim.surface);
        }
        park_pump(20);
    }

    printf("victim frame callbacks in 1.2s: %d\n", frames_seen);
    if (frames_seen < 30) {
        fprintf(stderr, "compositor frame loop stalled by unsignaled acquire points (%d callbacks in 1.2s)\n", frames_seen);
        return 1;
    }

    /* a frame callback queued behind the parked content: it may only fire
     * once the acquire points signal and the parked commits apply */
    frames_seen = 0;
    cb = wl_surface_frame(hostile.surface);
    wl_callback_add_listener(cb, &park_frame_listener, NULL);
    wl_surface_commit(hostile.surface);
    wl_display_flush(wl_dpy);

    drmSyncobjTimelineSignal(sync_signal_fd, &sync_signal_handle, &point, 1);

    start = park_now_ms();
    while (!frames_seen && park_now_ms() - start < 3000) {
        park_pump(50);
    }
    if (!frames_seen) {
        fprintf(stderr, "parked content did not resume after the acquire points signaled\n");
        return 1;
    }

    return 0;
}
