/* Explicit-sync points riding a sync subsurface's cache: a dmabuf committed
 * with acquire/release points into the cache and replaced there before the
 * parent commits must have its release point signaled; the replacement
 * applies with its points on the parent commit, and once desynced the
 * surface's points apply with its own commit. Exits 77 without explicit sync or a
 * dmabuf source. */
#include "syncobj_error.inc"

/* a timeline whose drm fd and handle stay with the test, to signal and wait */
struct kept_timeline {
    struct wp_linux_drm_syncobj_timeline_v1* obj;
    int fd;
    uint32_t handle;
};

static int keep_timeline(struct kept_timeline* t) {
    t->fd = sync_test_drm_fd();
    if (t->fd < 0) return 0;

    int timeline_fd = -1;

    if (drmSyncobjCreate(t->fd, 0, &t->handle) != 0 || drmSyncobjHandleToFD(t->fd, t->handle, &timeline_fd) != 0) {
        return 0;
    }
    t->obj = wp_linux_drm_syncobj_manager_v1_import_timeline(sync_manager, timeline_fd);
    close(timeline_fd);
    return 1;
}

static void signal_point(struct kept_timeline* t, uint64_t point) {
    drmSyncobjTimelineSignal(t->fd, &t->handle, &point, 1);
}

/* waits up to 3 s for the point, pumping the connection meanwhile */
static int point_signaled(struct kept_timeline* t, uint64_t point) {
    for (int i = 0; i < 300; i++) {
        uint64_t p = point;

        if (drmSyncobjTimelineWait(t->fd, &t->handle, &p, 1, 0, 0, NULL) == 0) return 1;
        wl_display_roundtrip(wl_dpy);
        usleep(10000);
    }
    return 0;
}

static struct wl_buffer* dumb_dmabuf(void) {
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
            drmPrimeHandleToFD(fd, create.handle, DRM_CLOEXEC | DRM_RDWR, &prime) != 0 || prime < 0) {
            close(fd);
            continue;
        }
        close(fd);
        struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(sync_dmabuf);
        zwp_linux_buffer_params_v1_add(params, prime, 0, 0, create.pitch, 0, 0);
        close(prime);
        return zwp_linux_buffer_params_v1_create_immed(params, 1, 1, 0x34325241u, 0);
    }
    return NULL;
}

static struct wl_buffer* any_dmabuf(void) {
    if (!sync_dmabuf || !sync_linear_argb) return NULL;

    struct wl_buffer* b = sync_test_dmabuf();

    return b ? b : dumb_dmabuf();
}

static void commit_with_points(struct wp_linux_drm_syncobj_surface_v1* ss, struct wl_surface* s,
                               struct kept_timeline* acq, struct kept_timeline* rel, uint64_t point,
                               struct wl_buffer* b) {
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(ss, acq->obj, (uint32_t)(point >> 32), (uint32_t)point);
    wp_linux_drm_syncobj_surface_v1_set_release_point(ss, rel->obj, (uint32_t)(point >> 32), (uint32_t)point);
    wl_surface_attach(s, b, 0, 0);
    wl_surface_damage(s, 0, 0, 1, 1);
    wl_surface_commit(s);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    int rc = sync_test_boot();
    if (rc) return rc;

    struct kept_timeline acq, rel;
    struct wl_buffer* first = any_dmabuf();
    struct wl_buffer* second = any_dmabuf();
    struct wl_buffer* third = any_dmabuf();

    if (!keep_timeline(&acq) || !keep_timeline(&rel) || !first || !second || !third || !wl_subcomp) return 77;

    struct wl_toplevel_ctx top;
    wl_make_toplevel(&top, "syncobj-cached", 120, 90, 0xff2040c0);

    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);

    struct wp_linux_drm_syncobj_surface_v1* ss = wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, child);

    /* every acquire point is materialized up front: the test is about the
     * cache, not about parking */
    signal_point(&acq, 1);
    signal_point(&acq, 2);
    signal_point(&acq, 3);

    commit_with_points(ss, child, &acq, &rel, 1, first);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "caching a dmabuf with sync points failed\n");
        return 1;
    }
    commit_with_points(ss, child, &acq, &rel, 2, second);
    if (!point_signaled(&rel, 1)) {
        fprintf(stderr, "the replaced cached dmabuf's release point was not signaled\n");
        return 1;
    }

    wl_surface_commit(top.surface);
    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "applying the cached dmabuf with its points failed\n");
        return 1;
    }

    /* desync now, the surface applies its points with its own commit; the
     * buffer it replaces is released through point 2 */
    wl_subsurface_set_desync(sub);
    commit_with_points(ss, child, &acq, &rel, 3, third);
    if (!point_signaled(&rel, 2)) {
        fprintf(stderr, "the applied dmabuf's release point was not signaled once replaced\n");
        return 1;
    }

    printf("syncobj cached ok\n");
    return 0;
}
