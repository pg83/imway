/* linux-drm-syncobj on content that actually reaches the screen. A window
 * shows a dmabuf whose acquire point is already signaled; when a second
 * buffer replaces it, the first buffer's release point must signal. A
 * commit without a buffer needs no points. On a synchronized subsurface a
 * buffer committed and then replaced before the parent commits is dropped
 * unshown: its release point signals as well. Last, the syncobj surface
 * object and the timelines are destroyed while the surfaces live. */
#include "syncobj_error.inc"

#include <time.h>

struct timeline {
    struct wp_linux_drm_syncobj_timeline_v1* proxy;
    int fd;
    uint32_t handle;
};

static int make_timeline(struct timeline* t) {
    t->fd = sync_test_drm_fd();
    if (t->fd < 0) return -1;

    int exported = -1;

    if (drmSyncobjCreate(t->fd, 0, &t->handle) != 0 || drmSyncobjHandleToFD(t->fd, t->handle, &exported) != 0) {
        return -1;
    }

    t->proxy = wp_linux_drm_syncobj_manager_v1_import_timeline(sync_manager, exported);
    close(exported);

    return 0;
}

static void signal_point(struct timeline* t, uint64_t point) {
    drmSyncobjTimelineSignal(t->fd, &t->handle, &point, 1);
}

/* wait up to three seconds for a point to be signaled */
static int point_signaled(struct timeline* t, uint64_t point) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    int64_t deadline = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec + 3000000000ll;

    for (;;) {
        uint32_t h = t->handle;
        uint64_t p = point;

        if (drmSyncobjTimelineWait(t->fd, &h, &p, 1, 0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL) == 0) {
            return 1;
        }

        clock_gettime(CLOCK_MONOTONIC, &ts);

        if ((int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec > deadline) {
            return 0;
        }

        /* the compositor signals from its frame loop: keep it fed */
        wl_display_roundtrip(wl_dpy);
        usleep(20000);
    }
}

static void commit_synced(struct wp_linux_drm_syncobj_surface_v1* sync_surface, struct wl_surface* surface,
                          struct wl_buffer* buffer, struct timeline* acq, struct timeline* rel, uint64_t point) {
    signal_point(acq, point);
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(sync_surface, acq->proxy, (uint32_t)(point >> 32), (uint32_t)point);
    wp_linux_drm_syncobj_surface_v1_set_release_point(sync_surface, rel->proxy, (uint32_t)(point >> 32), (uint32_t)point);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, 32, 32);
    wl_surface_commit(surface);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);

    int rc = sync_test_boot();

    if (rc) return rc;

    struct timeline acq, rel, sub_acq, sub_rel;

    if (make_timeline(&acq) || make_timeline(&rel) || make_timeline(&sub_acq) || make_timeline(&sub_rel)) return 77;

    struct wl_buffer* first = sync_dumb_dmabuf(32, 32);
    struct wl_buffer* second = sync_dumb_dmabuf(32, 32);
    struct wl_buffer* dropped = sync_dumb_dmabuf(32, 32);
    struct wl_buffer* kept = sync_dumb_dmabuf(32, 32);

    if (!first || !second || !dropped || !kept || !wl_subcomp) return 77;

    struct wl_toplevel_ctx top;

    wl_make_toplevel(&top, "syncobj-present", 32, 32, 0xff40a040u);

    struct wp_linux_drm_syncobj_surface_v1* sync_surface =
        wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, top.surface);

    commit_synced(sync_surface, top.surface, first, &acq, &rel, 1);
    wl_display_roundtrip(wl_dpy);
    commit_synced(sync_surface, top.surface, second, &acq, &rel, 2);

    if (!point_signaled(&rel, 1)) {
        fprintf(stderr, "the replaced buffer's release point never signaled\n");
        return 1;
    }

    printf("release signaled\n");

    /* no buffer, no points: nothing to object to */
    wl_surface_damage(top.surface, 0, 0, 1, 1);
    wl_surface_commit(top.surface);

    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "a bufferless commit on a syncobj surface was refused\n");
        return 1;
    }

    /* a synchronized subsurface: the first buffer is replaced in the cache
     * before the parent ever commits, so it is dropped unshown */
    struct wl_surface* child = wl_compositor_create_surface(wl_comp);
    struct wl_subsurface* sub = wl_subcompositor_get_subsurface(wl_subcomp, child, top.surface);
    struct wp_linux_drm_syncobj_surface_v1* child_sync =
        wp_linux_drm_syncobj_manager_v1_get_surface(sync_manager, child);

    (void)sub;
    commit_synced(child_sync, child, dropped, &sub_acq, &sub_rel, 1);
    commit_synced(child_sync, child, kept, &sub_acq, &sub_rel, 2);
    commit_synced(sync_surface, top.surface, first, &acq, &rel, 3);

    if (!point_signaled(&sub_rel, 1)) {
        fprintf(stderr, "the dropped cached buffer's release point never signaled\n");
        return 1;
    }

    printf("cached release signaled\n");

    /* the protocol objects go while the surfaces live */
    wp_linux_drm_syncobj_surface_v1_destroy(sync_surface);
    wp_linux_drm_syncobj_surface_v1_destroy(child_sync);
    wp_linux_drm_syncobj_timeline_v1_destroy(acq.proxy);
    wp_linux_drm_syncobj_timeline_v1_destroy(rel.proxy);
    wp_linux_drm_syncobj_timeline_v1_destroy(sub_acq.proxy);
    wp_linux_drm_syncobj_timeline_v1_destroy(sub_rel.proxy);

    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "destroying the syncobj objects failed\n");
        return 1;
    }

    /* and the surface takes implicit-sync content again */
    wl_surface_attach(top.surface, wl_solid(32, 32, 0xff2020c0u), 0, 0);
    wl_surface_damage(top.surface, 0, 0, 32, 32);
    wl_surface_commit(top.surface);

    if (wl_display_roundtrip(wl_dpy) < 0) {
        fprintf(stderr, "the surface refused a buffer once its syncobj object was gone\n");
        return 1;
    }

    printf("syncobj present done\n");

    return 0;
}
